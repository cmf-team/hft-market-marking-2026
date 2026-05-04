#include "catch2/catch_all.hpp"
#include "backtest/engine/engine.hpp"
#include <vector>

// Helper: build a minimal LOB snapshot
static LobSnapshot make_lob(uint64_t ts, int64_t bid, int64_t ask)
{
    LobSnapshot s{};
    s.timestamp = ts;
    s.bids[0] = {bid, 1000000};
    s.asks[0] = {ask, 1000000};
    return s;
}

// TEST 1: PnL round-trip: buy 1000 @ 100.0, sell 1000 @ 100.1 → realized > 0
TEST_CASE("BacktestEngineTest - PnlRoundTrip", "[BacktestEngineTest]")
{
    PnlState pnl{};
    Fill buy{1000ULL, 1, 100'0000000LL, 1000, Side::Buy, 0.0};
    pnl.apply_fill(buy);
    CHECK(pnl.position == 1000);

    Fill sell{2000ULL, 2, 100'1000000LL, 1000, Side::Sell, 0.0};
    pnl.apply_fill(sell);
    CHECK(pnl.position == 0);
    CHECK(pnl.realized_pnl > 0.0);
}

// TEST 2: LOB cross — buy fills when best ask drops to/below limit
TEST_CASE("BacktestEngineTest - BuyOrderFillsOnLobCross", "[BacktestEngineTest]")
{
    OrderBook ob{};
    PnlState pnl{};
    Config cfg{};
    Order o{};
    o.id = 1;
    o.price = 100'0000000LL;
    o.qty = 1000;
    o.side = Side::Buy;
    o.placement_ts = 1000ULL;
    ob.submit(o);
    LobSnapshot lob = make_lob(2000ULL, 99'0000000LL, 100'0000000LL);
    std::vector<Fill> fills;
    try_fill_on_lob(ob, lob, fills, pnl, cfg);
    REQUIRE(!fills.empty());
    CHECK(fills[0].qty == 1000);
    CHECK(fills[0].price == 100'0000000LL);
    CHECK(pnl.position == 1000);
}

// TEST 3: LOB cross — same-tick look-ahead guard
TEST_CASE("BacktestEngineTest - BuyNoFillOnSameTickLob", "[BacktestEngineTest]")
{
    OrderBook ob{};
    PnlState pnl{};
    Config cfg{};
    Order o{};
    o.id = 1;
    o.price = 100'0000000LL;
    o.qty = 1000;
    o.side = Side::Buy;
    o.placement_ts = 5000ULL;
    ob.submit(o);
    LobSnapshot lob = make_lob(5000ULL, 99'0000000LL, 99'5000000LL);
    std::vector<Fill> fills;
    try_fill_on_lob(ob, lob, fills, pnl, cfg);
    CHECK(fills.empty());
}

// TEST 4: LOB no-cross — sell at 101 stays unfilled when bid < 101
TEST_CASE("BacktestEngineTest - SellOrderNoFillWhenBidBelow", "[BacktestEngineTest]")
{
    OrderBook ob{};
    PnlState pnl{};
    Config cfg{};
    Order o{};
    o.id = 1;
    o.price = 101'0000000LL;
    o.qty = 1000;
    o.side = Side::Sell;
    o.placement_ts = 1000ULL;
    ob.submit(o);
    LobSnapshot lob = make_lob(2000ULL, 100'0000000LL, 100'5000000LL);
    std::vector<Fill> fills;
    try_fill_on_lob(ob, lob, fills, pnl, cfg);
    CHECK(fills.empty());
}

// TEST 5: Partial fill preserves queue position at same price level
TEST_CASE("BacktestEngineTest - PartialFillPreservesQueuePosition", "[BacktestEngineTest]")
{
    OrderBook ob{};
    PnlState pnl{};
    Config cfg{};

    // Order A: qty=2'000'000, placement_ts=1000
    Order a{};
    a.id = 1;
    a.price = 100'0000000LL;
    a.qty = 2'000'000;
    a.side = Side::Buy;
    a.placement_ts = 1000ULL;
    ob.submit(a);

    // Order B: qty=1'000'000, placement_ts=1001 (same price level, behind A)
    Order b{};
    b.id = 2;
    b.price = 100'0000000LL;
    b.qty = 1'000'000;
    b.side = Side::Buy;
    b.placement_ts = 1001ULL;
    ob.submit(b);

    // LOB snapshot at ts=2000 with only 1'000'000 liquidity at best ask
    // Set asks[0] to the exact crossing price, and asks[1..24] to a non-crossing price
    // to avoid false crossings on garbage data
    LobSnapshot lob1{};
    lob1.timestamp = 2000ULL;
    lob1.asks[0] = {100'0000000LL, 1'000'000}; // crosses with buy @ 100
    lob1.bids[0] = {99'0000000LL, 1'000'000};
    // Initialize all other ask levels to a price above order (no cross)
    for (int i = 1; i < LOB_DEPTH; ++i)
    {
        lob1.asks[i] = {101'0000000LL, 0};
        lob1.bids[i] = {98'0000000LL, 0};
    }

    std::vector<Fill> fills;
    try_fill_on_lob(ob, lob1, fills, pnl, cfg);

    // After first fill: A should be partially filled and still at front
    CHECK(fills.size() == 1);
    CHECK(fills[0].order_id == 1); // A's first fill
    CHECK(fills[0].qty == 1'000'000);

    // Verify A is still at front (not moved to tail)
    bool found = ob.match(Side::Buy, [&](std::reference_wrapper<Order> best_ref)
                          {
        Order& best = best_ref.get();
        CHECK(best.id == 1);  // A is still front
        CHECK(best.filled == 1'000'000);
        CHECK(best.remaining() == 1'000'000); });
    REQUIRE(found);

    // Second LOB snapshot with more liquidity — enough to fill A completely + B partially
    LobSnapshot lob2{};
    lob2.timestamp = 3000ULL;
    lob2.asks[0] = {100'0000000LL, 2'000'000}; // enough to fill A's remaining 1M + B's 1M
    lob2.bids[0] = {99'0000000LL, 1'000'000};
    for (int i = 1; i < LOB_DEPTH; ++i)
    {
        lob2.asks[i] = {101'0000000LL, 0};
        lob2.bids[i] = {98'0000000LL, 0};
    }

    try_fill_on_lob(ob, lob2, fills, pnl, cfg);

    // Now we should have fills from A and B, in order
    CHECK(fills.size() == 3);
    CHECK(fills[1].order_id == 1); // A's second fill (completes A)
    CHECK(fills[1].qty == 1'000'000);
    CHECK(fills[2].order_id == 2); // B's fill
    CHECK(fills[2].qty == 1'000'000);

    // OrderBook should be empty now
    CHECK(ob.empty(Side::Buy));
}

// TEST 6: Multiple orders deplete the same LOB level
// This test explicitly covers the bug fix: when order A fully consumes level[0],
// order B (behind A in queue) should NOT re-fill from the same depleted level.
TEST_CASE("BacktestEngineTest - MultipleOrdersDepleteSameLobLevel", "[BacktestEngineTest]")
{
    OrderBook ob{};
    PnlState pnl{};
    Config cfg{};

    // Order A: qty=10 @ 100.0
    Order a{};
    a.id = 1;
    a.price = 100'0000000LL;
    a.qty = 10;
    a.side = Side::Buy;
    a.placement_ts = 1000ULL;
    ob.submit(a);

    // Order B: qty=5 @ 100.0 (behind A in queue)
    Order b{};
    b.id = 2;
    b.price = 100'0000000LL;
    b.qty = 5;
    b.side = Side::Buy;
    b.placement_ts = 1001ULL;
    ob.submit(b);

    // LOB snapshot with exactly 10 liquidity at best ask level
    // level[0] has 10, level[1] has 0 (no secondary liquidity)
    LobSnapshot lob{};
    lob.timestamp = 2000ULL;
    lob.asks[0] = {100'0000000LL, 10}; // Exactly enough for A, none left for B
    lob.bids[0] = {99'0000000LL, 1000};
    for (int i = 1; i < LOB_DEPTH; ++i)
    {
        lob.asks[i] = {101'0000000LL, 0}; // Non-crossing levels
        lob.bids[i] = {98'0000000LL, 0};
    }

    std::vector<Fill> fills;
    try_fill_on_lob(ob, lob, fills, pnl, cfg);

    // Only A should fill: 10 from level[0]
    // B should remain unfilled because level[0] is now depleted
    CHECK(fills.size() == 1);
    CHECK(fills[0].order_id == 1); // A's fill
    CHECK(fills[0].qty == 10);
    CHECK(fills[0].price == 100'0000000LL);

    // B should still be in the order book, unfilled
    bool found = ob.match(Side::Buy, [&](std::reference_wrapper<Order> best_ref)
                          {
        Order& best = best_ref.get();
        CHECK(best.id == 2);  // B is now at the front
        CHECK(best.filled == 0);
        CHECK(best.remaining() == 5); });
    REQUIRE(found);
}
