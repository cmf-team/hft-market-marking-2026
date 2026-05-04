#include "catch2/catch_all.hpp"
#include "backtest/order_book/order_book.hpp"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{

// Build an Order with sensible defaults; only supply what the test cares about.
Order make_order(const uint64_t id, const Side side, const int64_t price_ticks, const int64_t qty = 100,
                 const uint64_t ts = 0)
{
    Order o;
    o.id = id;
    o.side = side;
    o.price = price_ticks;
    o.qty = qty;
    o.filled = 0;
    o.placement_ts = ts;
    return o;
}

// Shorthand tick constructors (1 tick = 1e-7 price unit).
// e.g. ticks(1, 1000000) == 1.1 in raw price
int64_t ticks(const int64_t whole, const int64_t frac = 0)
{
    return whole * PRICE_SCALE + frac;
}

} // namespace

struct OrderBookTestFixture
{
    OrderBook book;
};

// ===========================================================================
// empty() — initial state
// ===========================================================================

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - EmptyOnConstruction_BuySide", "[OrderBookTest]")
{
    CHECK(book.empty(Side::Buy));
}

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - EmptyOnConstruction_SellSide", "[OrderBookTest]")
{
    CHECK(book.empty(Side::Sell));
}

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - PopOnEmpty_DoesNotCrash", "[OrderBookTest]")
{
    // match returns false on empty book (no callback invoked)
    bool called = false;
    CHECK(!book.match(Side::Buy, [&](std::reference_wrapper<Order>)
                            { called = true; }));
    CHECK(!called);
}

// ===========================================================================
// submit() — basic insertion
// ===========================================================================

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - SubmitBid_BookBecomesNonEmpty", "[OrderBookTest]")
{
    book.submit(make_order(1, Side::Buy, ticks(100)));
    CHECK(!book.empty(Side::Buy));
}

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - SubmitAsk_BookBecomesNonEmpty", "[OrderBookTest]")
{
    book.submit(make_order(1, Side::Sell, ticks(101)));
    CHECK(!book.empty(Side::Sell));
}

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - SubmitBid_DoesNotAffectAskSide", "[OrderBookTest]")
{
    book.submit(make_order(1, Side::Buy, ticks(100)));
    CHECK(book.empty(Side::Sell));
}

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - SubmitAsk_DoesNotAffectBidSide", "[OrderBookTest]")
{
    book.submit(make_order(1, Side::Sell, ticks(101)));
    CHECK(book.empty(Side::Buy));
}

// ===========================================================================
// top() — best price selection
// ===========================================================================

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_ReturnsSingleBid", "[OrderBookTest]")
{
    auto o = make_order(1, Side::Buy, ticks(100), 50);
    book.submit(o);
    bool found = false;
    bool matched = book.match(Side::Buy, [&](std::reference_wrapper<Order> t_ref)
                              {
        found = true;
        Order& t = t_ref.get();
        CHECK(t.id == 1u);
        CHECK(t.price == ticks(100));
        CHECK(t.qty == 50); });
    CHECK((matched && found));
}

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_ReturnsSingleAsk", "[OrderBookTest]")
{
    auto o = make_order(2, Side::Sell, ticks(101), 30);
    book.submit(o);
    bool found = false;
    bool matched = book.match(Side::Sell, [&](std::reference_wrapper<Order> t_ref)
                              {
        found = true;
        Order& t = t_ref.get();
        CHECK(t.id == 2u);
        CHECK(t.price == ticks(101)); });
    CHECK((matched && found));
}

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_BestBid_IsHighestPrice", "[OrderBookTest]")
{
    // Submit three bids at different prices; best bid = highest.
    book.submit(make_order(1, Side::Buy, ticks(99)));
    book.submit(make_order(2, Side::Buy, ticks(101)));
    book.submit(make_order(3, Side::Buy, ticks(100)));

    bool found = false;
    CHECK(book.match(Side::Buy, [&](std::reference_wrapper<Order> t_ref)
                           {
        found = true;
        Order& t = t_ref.get();
        CHECK(t.id == 2u);
        CHECK(t.price == ticks(101)); }));
    CHECK(found);
}

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_BestAsk_IsLowestPrice", "[OrderBookTest]")
{
    // Submit three asks at different prices; best ask = lowest.
    book.submit(make_order(1, Side::Sell, ticks(103)));
    book.submit(make_order(2, Side::Sell, ticks(101)));
    book.submit(make_order(3, Side::Sell, ticks(102)));

    bool found = false;
    CHECK(book.match(Side::Sell, [&](std::reference_wrapper<Order> t_ref)
                           {
        found = true;
        Order& t = t_ref.get();
        CHECK(t.id == 2u);
        CHECK(t.price == ticks(101)); }));
    CHECK(found);
}

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_DoesNotMutateBook_WhenPartiallyFilled", "[OrderBookTest]")
{
    book.submit(make_order(1, Side::Buy, ticks(100)));
    // match with partial fill should not pop
    bool found = book.match(Side::Buy, [&](std::reference_wrapper<Order> o_ref)
                            {
                                o_ref.get().filled = 0; // partial fill
                            });
    CHECK(found);
    // Order should still be there with same id
    found = false;
    CHECK(book.match(Side::Buy, [&](std::reference_wrapper<Order> o_ref)
                           {
        found = true;
        CHECK(o_ref.get().id == 1u); }));
    CHECK(found);
}

// ===========================================================================
// FIFO ordering within the same price level
// ===========================================================================

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_FifoWithinPriceLevel_BuySide", "[OrderBookTest]")
{
    // All three bids at the same price — must come out in submission order.
    book.submit(make_order(10, Side::Buy, ticks(100)));
    book.submit(make_order(11, Side::Buy, ticks(100)));
    book.submit(make_order(12, Side::Buy, ticks(100)));

    // Consume first order completely
    CHECK(book.match(Side::Buy, [&](std::reference_wrapper<Order> o)
                           {
                               CHECK(o.get().id == 10u);
                               o.get().filled = o.get().qty; // fully consume
                           }));
    // Consume second order completely
    CHECK(book.match(Side::Buy, [&](std::reference_wrapper<Order> o)
                           {
        CHECK(o.get().id == 11u);
        o.get().filled = o.get().qty; }));
    // Consume third order completely
    CHECK(book.match(Side::Buy, [&](std::reference_wrapper<Order> o)
                           {
        CHECK(o.get().id == 12u);
        o.get().filled = o.get().qty; }));
    // Book should be empty
    CHECK(!book.match(Side::Buy, [&](std::reference_wrapper<Order>) {}));
}

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_FifoWithinPriceLevel_SellSide", "[OrderBookTest]")
{
    book.submit(make_order(20, Side::Sell, ticks(101)));
    book.submit(make_order(21, Side::Sell, ticks(101)));
    book.submit(make_order(22, Side::Sell, ticks(101)));

    CHECK(book.match(Side::Sell, [&](std::reference_wrapper<Order> o)
                           {
        CHECK(o.get().id == 20u);
        o.get().filled = o.get().qty; }));
    CHECK(book.match(Side::Sell, [&](std::reference_wrapper<Order> o)
                           {
        CHECK(o.get().id == 21u);
        o.get().filled = o.get().qty; }));
    CHECK(book.match(Side::Sell, [&](std::reference_wrapper<Order> o)
                           {
        CHECK(o.get().id == 22u);
        o.get().filled = o.get().qty; }));
    CHECK(!book.match(Side::Sell, [&](std::reference_wrapper<Order>) {}));
}

// ===========================================================================
// pop() — removal semantics
// ===========================================================================

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_FullyConsumeBid_BookBecomesEmpty", "[OrderBookTest]")
{
    book.submit(make_order(1, Side::Buy, ticks(100)));
    CHECK(book.match(Side::Buy, [&](std::reference_wrapper<Order> o)
                           {
                               o.get().filled = o.get().qty; // fully consume
                           }));
    CHECK(book.empty(Side::Buy));
    CHECK(!book.match(Side::Buy, [&](std::reference_wrapper<Order>) {}));
}

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_FullyConsumeAsk_BookBecomesEmpty", "[OrderBookTest]")
{
    book.submit(make_order(1, Side::Sell, ticks(101)));
    CHECK(book.match(Side::Sell, [&](std::reference_wrapper<Order> o)
                           { o.get().filled = o.get().qty; }));
    CHECK(book.empty(Side::Sell));
    CHECK(!book.match(Side::Sell, [&](std::reference_wrapper<Order>) {}));
}

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_AutoPopBestBid_NextBestSurfaces", "[OrderBookTest]")
{
    book.submit(make_order(1, Side::Buy, ticks(100)));
    book.submit(make_order(2, Side::Buy, ticks(102))); // best
    book.submit(make_order(3, Side::Buy, ticks(101)));

    // Consume id=2 completely
    CHECK(book.match(Side::Buy, [&](std::reference_wrapper<Order> o)
                           {
        CHECK(o.get().id == 2u);
        o.get().filled = o.get().qty; }));

    // Next best should now be id=3 @ 101
    CHECK(book.match(Side::Buy, [&](std::reference_wrapper<Order> o)
                           {
        CHECK(o.get().id == 3u);
        CHECK(o.get().price == ticks(101)); }));
}

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_AutoPopBestAsk_NextBestSurfaces", "[OrderBookTest]")
{
    book.submit(make_order(1, Side::Sell, ticks(103)));
    book.submit(make_order(2, Side::Sell, ticks(101))); // best
    book.submit(make_order(3, Side::Sell, ticks(102)));

    // Consume id=2 completely
    CHECK(book.match(Side::Sell, [&](std::reference_wrapper<Order> o)
                           {
        CHECK(o.get().id == 2u);
        o.get().filled = o.get().qty; }));

    // Next best should now be id=3 @ 102
    CHECK(book.match(Side::Sell, [&](std::reference_wrapper<Order> o)
                           {
        CHECK(o.get().id == 3u);
        CHECK(o.get().price == ticks(102)); }));
}

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_AffectsOnlySide", "[OrderBookTest]")
{
    book.submit(make_order(1, Side::Buy, ticks(100)));
    book.submit(make_order(2, Side::Sell, ticks(101)));

    // Consume buy side completely
    CHECK(book.match(Side::Buy, [&](std::reference_wrapper<Order> o)
                           { o.get().filled = o.get().qty; }));

    CHECK(book.empty(Side::Buy));
    CHECK(!book.empty(Side::Sell));
    CHECK(book.match(Side::Sell, [&](std::reference_wrapper<Order> o)
                           { CHECK(o.get().id == 2u); }));
}

// ===========================================================================
// Level cleanup — empty price levels must be erased from the map
// ===========================================================================

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_LevelCleanupAfterFullConsumption", "[OrderBookTest]")
{
    // Two levels; consume all orders from the better one.
    book.submit(make_order(1, Side::Buy, ticks(102)));
    book.submit(make_order(2, Side::Buy, ticks(100)));

    // Fully consume the best (id=1 @ 102)
    CHECK(book.match(Side::Buy, [&](std::reference_wrapper<Order> o)
                           {
        CHECK(o.get().id == 1u);
        o.get().filled = o.get().qty; }));

    // Next match should surface the order at 100, not null
    CHECK(book.match(Side::Buy, [&](std::reference_wrapper<Order> o)
                           { CHECK(o.get().price == ticks(100)); }));
}

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_LevelCleanupMultipleOrdersSameLevel", "[OrderBookTest]")
{
    // Two orders at price 100, one at 99.  Consume both at 100; 99 should surface.
    book.submit(make_order(1, Side::Buy, ticks(100)));
    book.submit(make_order(2, Side::Buy, ticks(100)));
    book.submit(make_order(3, Side::Buy, ticks(99)));

    // Consume first @ 100
    CHECK(book.match(Side::Buy, [&](std::reference_wrapper<Order> o)
                           { o.get().filled = o.get().qty; }));

    // Consume second @ 100
    CHECK(book.match(Side::Buy, [&](std::reference_wrapper<Order> o)
                           { o.get().filled = o.get().qty; }));

    // Third should be @ 99
    REQUIRE(!book.empty(Side::Buy));
    CHECK(book.match(Side::Buy, [&](std::reference_wrapper<Order> o)
                           { CHECK(o.get().price == ticks(99)); }));
}

// ===========================================================================
// Price scale / tick precision
// ===========================================================================

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_TickPrecision_SubUnitPrices", "[OrderBookTest]")
{
    // Prices that differ only in sub-unit fractional ticks.
    const int64_t p1 = ticks(1, 1'000'000); // 1.1000000
    const int64_t p2 = ticks(1, 1'000'001); // 1.1000001  ← higher by 1 tick
    const int64_t p3 = ticks(1, 999'999);   // 1.0999999

    book.submit(make_order(1, Side::Buy, p3));
    book.submit(make_order(2, Side::Buy, p1));
    book.submit(make_order(3, Side::Buy, p2)); // best

    bool found = false;
    (void)book.match(Side::Buy, [&](std::reference_wrapper<Order> o)
                     {
        found = true;
        CHECK(o.get().id == 3u);
        CHECK(o.get().price == p2); });
    CHECK(found);
}

// ===========================================================================
// Order fields are preserved through submit/top round-trip
// ===========================================================================

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_OrderFieldsPreserved", "[OrderBookTest]")
{
    Order o;
    o.id = 42;
    o.side = Side::Sell;
    o.price = ticks(99, 5'000'000);
    o.qty = 250;
    o.filled = 10;
    o.placement_ts = 1'234'567'890ULL;

    book.submit(o);
    bool found = false;
    (void)book.match(Side::Sell, [&](std::reference_wrapper<Order> t_ref)
                     {
        found = true;
        Order& t = t_ref.get();
        CHECK(t.id == 42u);
        CHECK(t.price == ticks(99, 5'000'000));
        CHECK(t.qty == 250);
        CHECK(t.filled == 10);
        CHECK(t.placement_ts == 1'234'567'890ULL); });
    CHECK(found);
}

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_RemainingHelper_CorrectAfterPartialFill", "[OrderBookTest]")
{
    Order o = make_order(1, Side::Buy, ticks(100), 100);
    o.filled = 40;
    book.submit(o);
    bool found = false;
    (void)book.match(Side::Buy, [&](std::reference_wrapper<Order> o_ref)
                     {
        found = true;
        CHECK(o_ref.get().remaining() == 60); });
    CHECK(found);
}

// ===========================================================================
// Mixed bid/ask interaction
// ===========================================================================

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_BothSides_IndependentValues", "[OrderBookTest]")
{
    book.submit(make_order(1, Side::Buy, ticks(99)));
    book.submit(make_order(2, Side::Buy, ticks(100)));  // best bid
    book.submit(make_order(3, Side::Sell, ticks(101))); // best ask
    book.submit(make_order(4, Side::Sell, ticks(102)));

    bool found_bid = false, found_ask = false;
    (void)book.match(Side::Buy, [&](std::reference_wrapper<Order> o)
                     {
        found_bid = true;
        CHECK(o.get().id == 2u); });
    (void)book.match(Side::Sell, [&](std::reference_wrapper<Order> o)
                     {
        found_ask = true;
        CHECK(o.get().id == 3u); });
    CHECK(found_bid);
    CHECK(found_ask);
}

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_ExhaustAllOrders_BothSides", "[OrderBookTest]")
{
    book.submit(make_order(1, Side::Buy, ticks(100)));
    book.submit(make_order(2, Side::Sell, ticks(101)));

    // Exhaust buy side
    CHECK(book.match(Side::Buy, [&](std::reference_wrapper<Order> o)
                           { o.get().filled = o.get().qty; }));

    // Exhaust sell side
    CHECK(book.match(Side::Sell, [&](std::reference_wrapper<Order> o)
                           { o.get().filled = o.get().qty; }));

    CHECK(book.empty(Side::Buy));
    CHECK(book.empty(Side::Sell));
    CHECK(!book.match(Side::Buy, [&](std::reference_wrapper<Order>) {}));
    CHECK(!book.match(Side::Sell, [&](std::reference_wrapper<Order>) {}));
}

// ===========================================================================
// Stress: large number of price levels
// ===========================================================================

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_ManyPriceLevels_BestBidAlwaysHighest", "[OrderBookTest]")
{
    constexpr int N = 1000;
    for (int i = 1; i <= N; ++i)
    {
        book.submit(make_order(static_cast<uint64_t>(i), Side::Buy, ticks(i)));
    }

    // Verify best bid is the order at price ticks(1000)
    bool found_best = false;
    (void)book.match(Side::Buy, [&](std::reference_wrapper<Order> o)
                     {
        found_best = true;
        CHECK(o.get().price == ticks(N)); });
    CHECK(found_best);

    // Consume all and verify monotone descending order
    int64_t prev = ticks(N) + 1;
    for (int i = 0; i < N; ++i)
    {
        bool found = book.match(Side::Buy, [&](std::reference_wrapper<Order> o)
                                {
                                    CHECK(o.get().price < prev);
                                    prev = o.get().price;
                                    o.get().filled = o.get().qty; // consume
                                });
        REQUIRE(found);
    }
    CHECK(book.empty(Side::Buy));
}

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Match_ManyPriceLevels_BestAskAlwaysLowest", "[OrderBookTest]")
{
    constexpr int N = 1000;
    for (int i = 1; i <= N; ++i)
    {
        book.submit(make_order(static_cast<uint64_t>(i), Side::Sell, ticks(i)));
    }

    bool found_best = false;
    (void)book.match(Side::Sell, [&](std::reference_wrapper<Order> o)
                     {
        found_best = true;
        CHECK(o.get().price == ticks(1)); });
    CHECK(found_best);

    int64_t prev = ticks(0);
    for (int i = 0; i < N; ++i)
    {
        bool found = book.match(Side::Sell, [&](std::reference_wrapper<Order> o)
                                {
            CHECK(o.get().price > prev);
            prev = o.get().price;
            o.get().filled = o.get().qty; });
        REQUIRE(found);
    }
    CHECK(book.empty(Side::Sell));
}

TEST_CASE_METHOD(OrderBookTestFixture, "OrderBookTest - Drain_ClearsSidePreservingOther", "[OrderBookTest]")
{
    book.submit(make_order(1, Side::Buy, ticks(99)));
    book.submit(make_order(2, Side::Buy, ticks(98)));
    book.submit(make_order(3, Side::Sell, ticks(101)));
    book.submit(make_order(4, Side::Sell, ticks(102)));

    CHECK(!book.empty(Side::Buy));
    CHECK(!book.empty(Side::Sell));

    book.drain(Side::Buy);

    CHECK(book.empty(Side::Buy));
    CHECK(!book.empty(Side::Sell));

    bool found = false;
    (void)book.match(Side::Sell, [&](std::reference_wrapper<Order> o)
                     {
        found = true;
        CHECK(o.get().id == 3); });
    CHECK(found);
}
