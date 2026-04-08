#include "exec/matcher.hpp"

#include "bt/events.hpp"
#include "bt/order.hpp"
#include "bt/order_book.hpp"
#include "bt/types.hpp"

#include <gtest/gtest.h>

#include <cstddef>

namespace {

// Build a small synthetic book where best bid = bid_top and best ask = ask_top.
// Levels descend / ascend by 1 tick from the top with fixed amounts.
bt::BookSnapshot make_snapshot(bt::Timestamp ts, bt::Price bid_top, bt::Price ask_top) {
    bt::BookSnapshot s{};
    s.ts = ts;
    for (std::size_t i = 0; i < bt::kMaxLevels; ++i) {
        s.bids[i] = { bid_top - static_cast<bt::Price>(i), 100 };
        s.asks[i] = { ask_top + static_cast<bt::Price>(i), 100 };
    }
    return s;
}

bt::OrderBook book_with(bt::Price bid_top, bt::Price ask_top) {
    bt::OrderBook book;
    book.apply(make_snapshot(/*ts=*/0, bid_top, ask_top));
    return book;
}

// --------------------------------------------------------------------------
// submit() — post-only acceptance / rejection
// --------------------------------------------------------------------------

TEST(Matcher, SubmitAcceptsPassiveBuyBelowAsk) {
    bt::Matcher m;
    const auto book = book_with(100, 101);

    auto r = m.submit(bt::Side::Buy, /*price=*/100, /*qty=*/5, book, /*now=*/10);
    ASSERT_TRUE(r.accepted);
    EXPECT_EQ(r.id, 1u);
    EXPECT_TRUE(m.has_order(r.id));
    EXPECT_EQ(m.resting_count(), 1u);
}

TEST(Matcher, SubmitAcceptsPassiveSellAboveBid) {
    bt::Matcher m;
    const auto book = book_with(100, 101);

    auto r = m.submit(bt::Side::Sell, /*price=*/101, /*qty=*/5, book, /*now=*/10);
    ASSERT_TRUE(r.accepted);
    EXPECT_TRUE(m.has_order(r.id));
}

TEST(Matcher, SubmitAcceptsOnEmptyBook) {
    bt::Matcher m;
    const bt::OrderBook empty;
    ASSERT_TRUE(empty.empty());

    auto r = m.submit(bt::Side::Buy, /*price=*/12345, /*qty=*/1, empty, /*now=*/0);
    EXPECT_TRUE(r.accepted);
}

TEST(Matcher, PostOnlyRejectsBuyAtBestAsk) {
    bt::Matcher m;
    const auto book = book_with(100, 101);

    auto r = m.submit(bt::Side::Buy, /*price=*/101, /*qty=*/5, book, /*now=*/42);
    ASSERT_FALSE(r.accepted);
    EXPECT_EQ(r.reject.reason, bt::RejectReason::WouldCross);
    EXPECT_EQ(r.reject.id, 0u);  // never assigned
    EXPECT_EQ(r.reject.ts, 42);
    EXPECT_EQ(m.resting_count(), 0u);
}

TEST(Matcher, PostOnlyRejectsBuyAboveBestAsk) {
    bt::Matcher m;
    const auto book = book_with(100, 101);

    auto r = m.submit(bt::Side::Buy, /*price=*/200, /*qty=*/5, book, /*now=*/0);
    EXPECT_FALSE(r.accepted);
    EXPECT_EQ(r.reject.reason, bt::RejectReason::WouldCross);
}

TEST(Matcher, PostOnlyRejectsSellAtBestBid) {
    bt::Matcher m;
    const auto book = book_with(100, 101);

    auto r = m.submit(bt::Side::Sell, /*price=*/100, /*qty=*/5, book, /*now=*/0);
    EXPECT_FALSE(r.accepted);
    EXPECT_EQ(r.reject.reason, bt::RejectReason::WouldCross);
}

TEST(Matcher, SubmitAssignsIncrementingIds) {
    bt::Matcher m;
    const auto book = book_with(100, 101);

    auto a = m.submit(bt::Side::Buy, 99, 1, book, 0);
    auto b = m.submit(bt::Side::Buy, 98, 1, book, 0);
    auto c = m.submit(bt::Side::Sell, 102, 1, book, 0);

    ASSERT_TRUE(a.accepted && b.accepted && c.accepted);
    EXPECT_EQ(a.id, 1u);
    EXPECT_EQ(b.id, 2u);
    EXPECT_EQ(c.id, 3u);
}

// --------------------------------------------------------------------------
// cancel()
// --------------------------------------------------------------------------

TEST(Matcher, CancelRemovesRestingOrder) {
    bt::Matcher m;
    const auto book = book_with(100, 101);
    auto r = m.submit(bt::Side::Buy, 99, 5, book, 0);
    ASSERT_TRUE(r.accepted);

    m.cancel(r.id, /*now=*/1);
    EXPECT_FALSE(m.has_order(r.id));
    EXPECT_EQ(m.resting_count(), 0u);
}

TEST(Matcher, CancelUnknownIdIsNoOp) {
    bt::Matcher m;
    // Should not crash, should not throw, should leave state empty.
    m.cancel(/*id=*/999, /*now=*/0);
    EXPECT_EQ(m.resting_count(), 0u);
}

TEST(Matcher, CancelLeavesOtherOrdersAtSameLevelIntact) {
    bt::Matcher m;
    const auto book = book_with(100, 101);
    auto a = m.submit(bt::Side::Buy, 99, 5, book, 0);
    auto b = m.submit(bt::Side::Buy, 99, 7, book, 0);
    ASSERT_TRUE(a.accepted && b.accepted);

    m.cancel(a.id, 0);
    EXPECT_FALSE(m.has_order(a.id));
    EXPECT_TRUE(m.has_order(b.id));
    EXPECT_EQ(m.resting_count(), 1u);
}

// --------------------------------------------------------------------------
// on_trade() — fill rule
// --------------------------------------------------------------------------

TEST(Matcher, SellTradeFillsRestingBuyAtCrossingPrice) {
    bt::Matcher m;
    const auto book = book_with(100, 101);
    auto r = m.submit(bt::Side::Buy, /*price=*/99, /*qty=*/5, book, /*now=*/0);
    ASSERT_TRUE(r.accepted);

    // A sell trade at 99 (or below) crosses our 99 bid.
    auto fills = m.on_trade(bt::Trade{ /*ts=*/10, bt::Side::Sell, /*price=*/99, /*amount=*/5 }, /*now=*/10);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].id, r.id);
    EXPECT_EQ(fills[0].price, 99);   // resting limit price, NOT the trade price
    EXPECT_EQ(fills[0].qty, 5);
    EXPECT_EQ(fills[0].ts, 10);
    EXPECT_FALSE(m.has_order(r.id)); // fully filled, removed
}

TEST(Matcher, BuyTradeFillsRestingSellAtCrossingPrice) {
    bt::Matcher m;
    const auto book = book_with(100, 101);
    auto r = m.submit(bt::Side::Sell, /*price=*/102, /*qty=*/3, book, /*now=*/0);
    ASSERT_TRUE(r.accepted);

    auto fills = m.on_trade(bt::Trade{ 5, bt::Side::Buy, 102, 3 }, 5);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].id, r.id);
    EXPECT_EQ(fills[0].price, 102);
    EXPECT_EQ(fills[0].qty, 3);
}

TEST(Matcher, SellTradeAboveBidDoesNotFill) {
    bt::Matcher m;
    const auto book = book_with(100, 101);
    auto r = m.submit(bt::Side::Buy, 99, 5, book, 0);
    ASSERT_TRUE(r.accepted);

    // Sell trade at 100 — above our bid of 99 — must NOT fill us.
    auto fills = m.on_trade(bt::Trade{ 1, bt::Side::Sell, 100, 5 }, 1);
    EXPECT_TRUE(fills.empty());
    EXPECT_TRUE(m.has_order(r.id));
}

TEST(Matcher, BuyTradeBelowAskDoesNotFill) {
    bt::Matcher m;
    const auto book = book_with(100, 101);
    auto r = m.submit(bt::Side::Sell, 102, 5, book, 0);
    ASSERT_TRUE(r.accepted);

    auto fills = m.on_trade(bt::Trade{ 1, bt::Side::Buy, 101, 5 }, 1);
    EXPECT_TRUE(fills.empty());
    EXPECT_TRUE(m.has_order(r.id));
}

TEST(Matcher, OnTradePartialFillCappedByTradeAmount) {
    bt::Matcher m;
    const auto book = book_with(100, 101);
    auto r = m.submit(bt::Side::Buy, 99, /*qty=*/10, book, 0);
    ASSERT_TRUE(r.accepted);

    // Trade only 4 units — order should be partially filled.
    auto fills = m.on_trade(bt::Trade{ 1, bt::Side::Sell, 99, 4 }, 1);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].qty, 4);
    EXPECT_TRUE(m.has_order(r.id));  // still has 6 left

    // Second trade finishes it.
    auto fills2 = m.on_trade(bt::Trade{ 2, bt::Side::Sell, 99, 100 }, 2);
    ASSERT_EQ(fills2.size(), 1u);
    EXPECT_EQ(fills2[0].qty, 6);
    EXPECT_FALSE(m.has_order(r.id));
}

TEST(Matcher, OnTradePartialFillCappedByOrderQty) {
    bt::Matcher m;
    const auto book = book_with(100, 101);
    auto r = m.submit(bt::Side::Buy, 99, /*qty=*/3, book, 0);
    ASSERT_TRUE(r.accepted);

    // Trade has 100 units, order only wants 3 — fill caps at 3.
    auto fills = m.on_trade(bt::Trade{ 1, bt::Side::Sell, 99, 100 }, 1);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].qty, 3);
    EXPECT_FALSE(m.has_order(r.id));
}

TEST(Matcher, OnTradeFifoWithinSameLevel) {
    bt::Matcher m;
    const auto book = book_with(100, 101);
    auto a = m.submit(bt::Side::Buy, 99, 5, book, /*now=*/1);
    auto b = m.submit(bt::Side::Buy, 99, 5, book, /*now=*/2);
    ASSERT_TRUE(a.accepted && b.accepted);

    // Trade for 7 units: order `a` (oldest) gets all 5, order `b` gets 2.
    auto fills = m.on_trade(bt::Trade{ 3, bt::Side::Sell, 99, 7 }, 3);
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0].id, a.id);
    EXPECT_EQ(fills[0].qty, 5);
    EXPECT_EQ(fills[1].id, b.id);
    EXPECT_EQ(fills[1].qty, 2);
    EXPECT_FALSE(m.has_order(a.id));
    EXPECT_TRUE(m.has_order(b.id));
}

TEST(Matcher, OnTradePricePriorityBestFirst) {
    bt::Matcher m;
    const auto book = book_with(100, 101);
    // Two bids at different prices: 99 (best) and 98.
    auto worse  = m.submit(bt::Side::Buy, 98, 5, book, /*now=*/1);
    auto better = m.submit(bt::Side::Buy, 99, 5, book, /*now=*/2);  // submitted later but better price
    ASSERT_TRUE(worse.accepted && better.accepted);

    // Sell trade at 98 fills both (both bids cross 98). Best price first.
    auto fills = m.on_trade(bt::Trade{ 3, bt::Side::Sell, 98, 7 }, 3);
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0].id, better.id);
    EXPECT_EQ(fills[0].price, 99);   // filled at its OWN limit price
    EXPECT_EQ(fills[0].qty, 5);
    EXPECT_EQ(fills[1].id, worse.id);
    EXPECT_EQ(fills[1].price, 98);
    EXPECT_EQ(fills[1].qty, 2);
}

TEST(Matcher, OnTradeStopsAtFirstNonCrossingLevel) {
    bt::Matcher m;
    const auto book = book_with(100, 101);
    auto best  = m.submit(bt::Side::Buy, 99, 5, book, 0);
    auto worse = m.submit(bt::Side::Buy, 97, 5, book, 0);
    ASSERT_TRUE(best.accepted && worse.accepted);

    // Sell trade at 98 crosses the 99 bid but NOT the 97 bid.
    auto fills = m.on_trade(bt::Trade{ 1, bt::Side::Sell, 98, 100 }, 1);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].id, best.id);
    EXPECT_TRUE(m.has_order(worse.id));
}

TEST(Matcher, OnTradeWithNoRestingOrdersIsEmpty) {
    bt::Matcher m;
    auto fills = m.on_trade(bt::Trade{ 1, bt::Side::Sell, 99, 100 }, 1);
    EXPECT_TRUE(fills.empty());
}

// --------------------------------------------------------------------------
// on_snapshot() — empty stub in step 5
// --------------------------------------------------------------------------

TEST(Matcher, OnSnapshotIsEmptyStubInStep5) {
    bt::Matcher m;
    const auto prev = book_with(100, 101);
    const auto curr = book_with(100, 101);
    auto fills = m.on_snapshot(prev, curr, /*now=*/0);
    EXPECT_TRUE(fills.empty());
}

}  // namespace
