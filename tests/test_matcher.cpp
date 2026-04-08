#include "exec/matcher.hpp"

#include "bt/events.hpp"
#include "bt/order.hpp"
#include "bt/order_book.hpp"
#include "bt/queue_model.hpp"
#include "bt/types.hpp"

#include <gtest/gtest.h>

#include <cstddef>

namespace {

// Build a minimal book with ONLY top-of-book populated. Other levels stay
// at the default {0, 0}, so any test price other than bid_top/ask_top has
// volume_at == 0 and an order posted there starts with queue_ahead == 0.
// This is exactly what step 5's tests assumed implicitly; step 6 has
// dedicated tests for non-zero queue.
bt::BookSnapshot make_snapshot(bt::Timestamp ts, bt::Price bid_top, bt::Price ask_top) {
    bt::BookSnapshot s{};
    s.ts = ts;
    s.bids[0] = { bid_top, 100 };
    s.asks[0] = { ask_top, 100 };
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
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
    const auto book = book_with(100, 101);

    auto r = m.submit(bt::Side::Buy, /*price=*/100, /*qty=*/5, book, /*now=*/10);
    ASSERT_TRUE(r.accepted);
    EXPECT_EQ(r.id, 1u);
    EXPECT_TRUE(m.has_order(r.id));
    EXPECT_EQ(m.resting_count(), 1u);
}

TEST(Matcher, SubmitAcceptsPassiveSellAboveBid) {
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
    const auto book = book_with(100, 101);

    auto r = m.submit(bt::Side::Sell, /*price=*/101, /*qty=*/5, book, /*now=*/10);
    ASSERT_TRUE(r.accepted);
    EXPECT_TRUE(m.has_order(r.id));
}

TEST(Matcher, SubmitAcceptsOnEmptyBook) {
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
    const bt::OrderBook empty;
    ASSERT_TRUE(empty.empty());

    auto r = m.submit(bt::Side::Buy, /*price=*/12345, /*qty=*/1, empty, /*now=*/0);
    EXPECT_TRUE(r.accepted);
}

TEST(Matcher, PostOnlyRejectsBuyAtBestAsk) {
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
    const auto book = book_with(100, 101);

    auto r = m.submit(bt::Side::Buy, /*price=*/101, /*qty=*/5, book, /*now=*/42);
    ASSERT_FALSE(r.accepted);
    EXPECT_EQ(r.reject.reason, bt::RejectReason::WouldCross);
    EXPECT_EQ(r.reject.id, 0u);  // never assigned
    EXPECT_EQ(r.reject.ts, 42);
    EXPECT_EQ(m.resting_count(), 0u);
}

TEST(Matcher, PostOnlyRejectsBuyAboveBestAsk) {
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
    const auto book = book_with(100, 101);

    auto r = m.submit(bt::Side::Buy, /*price=*/200, /*qty=*/5, book, /*now=*/0);
    EXPECT_FALSE(r.accepted);
    EXPECT_EQ(r.reject.reason, bt::RejectReason::WouldCross);
}

TEST(Matcher, PostOnlyRejectsSellAtBestBid) {
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
    const auto book = book_with(100, 101);

    auto r = m.submit(bt::Side::Sell, /*price=*/100, /*qty=*/5, book, /*now=*/0);
    EXPECT_FALSE(r.accepted);
    EXPECT_EQ(r.reject.reason, bt::RejectReason::WouldCross);
}

TEST(Matcher, SubmitAssignsIncrementingIds) {
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
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
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
    const auto book = book_with(100, 101);
    auto r = m.submit(bt::Side::Buy, 99, 5, book, 0);
    ASSERT_TRUE(r.accepted);

    m.cancel(r.id, /*now=*/1);
    EXPECT_FALSE(m.has_order(r.id));
    EXPECT_EQ(m.resting_count(), 0u);
}

TEST(Matcher, CancelUnknownIdIsNoOp) {
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
    // Should not crash, should not throw, should leave state empty.
    m.cancel(/*id=*/999, /*now=*/0);
    EXPECT_EQ(m.resting_count(), 0u);
}

TEST(Matcher, CancelLeavesOtherOrdersAtSameLevelIntact) {
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
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
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
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
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
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
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
    const auto book = book_with(100, 101);
    auto r = m.submit(bt::Side::Buy, 99, 5, book, 0);
    ASSERT_TRUE(r.accepted);

    // Sell trade at 100 — above our bid of 99 — must NOT fill us.
    auto fills = m.on_trade(bt::Trade{ 1, bt::Side::Sell, 100, 5 }, 1);
    EXPECT_TRUE(fills.empty());
    EXPECT_TRUE(m.has_order(r.id));
}

TEST(Matcher, BuyTradeBelowAskDoesNotFill) {
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
    const auto book = book_with(100, 101);
    auto r = m.submit(bt::Side::Sell, 102, 5, book, 0);
    ASSERT_TRUE(r.accepted);

    auto fills = m.on_trade(bt::Trade{ 1, bt::Side::Buy, 101, 5 }, 1);
    EXPECT_TRUE(fills.empty());
    EXPECT_TRUE(m.has_order(r.id));
}

TEST(Matcher, OnTradePartialFillCappedByTradeAmount) {
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
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
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
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
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
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
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
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
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
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
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
    auto fills = m.on_trade(bt::Trade{ 1, bt::Side::Sell, 99, 100 }, 1);
    EXPECT_TRUE(fills.empty());
}

// --------------------------------------------------------------------------
// on_snapshot() — empty stub in step 5
// --------------------------------------------------------------------------

TEST(Matcher, OnSnapshotEmptyWhenBookUnchanged) {
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
    const auto prev = book_with(100, 101);
    const auto curr = book_with(100, 101);
    auto fills = m.on_snapshot(prev, curr, /*now=*/0);
    EXPECT_TRUE(fills.empty());
}

// --------------------------------------------------------------------------
// Step 6 — queue model integration
// --------------------------------------------------------------------------

// Build a book with explicit single-level depth on each side (used by tests
// that need queue_ahead > 0 at submit time).
bt::OrderBook deep_book(bt::Price bid_top, bt::Qty bid_qty,
                        bt::Price ask_top, bt::Qty ask_qty) {
    bt::BookSnapshot s{};
    s.ts = 0;
    s.bids[0] = { bid_top, bid_qty };
    s.asks[0] = { ask_top, ask_qty };
    bt::OrderBook b;
    b.apply(s);
    return b;
}

TEST(Matcher, SubmitInheritsQueueAheadFromBookVolume) {
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
    // 1000 lots resting at price 100 (best bid).
    const auto book = deep_book(/*bid_top=*/100, /*bid_qty=*/1000,
                                /*ask_top=*/200, /*ask_qty=*/500);

    auto r = m.submit(bt::Side::Buy, /*price=*/100, /*qty=*/5, book, 0);
    ASSERT_TRUE(r.accepted);

    // A small sell trade @ 100 — only 100 lots, less than the 1000 in front
    // of us. Order must NOT fill: this is the central correctness property
    // of the queue model.
    auto fills = m.on_trade(bt::Trade{ 1, bt::Side::Sell, 100, 100 }, 1);
    EXPECT_TRUE(fills.empty());
    EXPECT_TRUE(m.has_order(r.id));
}

TEST(Matcher, OrderFillsAfterEnoughTradeVolumeErodesQueue) {
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
    const auto book = deep_book(100, 100, 200, 500);

    auto r = m.submit(bt::Side::Buy, 100, /*qty=*/5, book, 0);
    ASSERT_TRUE(r.accepted);

    // Trade chews 60 of the 100 lots in front of us.
    auto f1 = m.on_trade(bt::Trade{ 1, bt::Side::Sell, 100, 60 }, 1);
    EXPECT_TRUE(f1.empty());
    EXPECT_TRUE(m.has_order(r.id));

    // Trade chews the remaining 40 + gives us 7 leftover. Fill caps at qty=5.
    auto f2 = m.on_trade(bt::Trade{ 2, bt::Side::Sell, 100, 47 }, 2);
    ASSERT_EQ(f2.size(), 1u);
    EXPECT_EQ(f2[0].id, r.id);
    EXPECT_EQ(f2[0].qty, 5);
    EXPECT_FALSE(m.has_order(r.id));
}

TEST(Matcher, QueueErosionThreadsThroughFifoOrdersAtSameLevel) {
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
    // Book has 10 lots at 100. Both orders join behind it: queue_ahead=10
    // for each (each order's queue is tracked independently — see
    // PessimisticQueueModel comments).
    const auto book = deep_book(100, 10, 200, 500);

    auto a = m.submit(bt::Side::Buy, 100, /*qty=*/5, book, /*now=*/1);
    auto b = m.submit(bt::Side::Buy, 100, /*qty=*/5, book, /*now=*/2);
    ASSERT_TRUE(a.accepted && b.accepted);

    // Trade of 25: A's queue (10) is consumed → 15 leftover → A fills 5
    // → 10 leftover threads to B → B's queue (10) consumed → 0 leftover
    // → B does not fill from this trade.
    auto fills = m.on_trade(bt::Trade{ 3, bt::Side::Sell, 100, 25 }, 3);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].id, a.id);
    EXPECT_EQ(fills[0].qty, 5);
    EXPECT_FALSE(m.has_order(a.id));
    EXPECT_TRUE(m.has_order(b.id));  // B still queued
}

TEST(Matcher, OnSnapshotEmitsFillWhenLevelDisappears) {
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
    const auto prev = deep_book(100, 500, 200, 500);

    // Submit when 100 is in the book — order has queue_ahead=500.
    auto r = m.submit(bt::Side::Buy, 100, /*qty=*/5, prev, 0);
    ASSERT_TRUE(r.accepted);

    // Build a snapshot where the 100 level disappeared (best bid is 99
    // now). The matcher's on_snapshot should emit a Case-A fill.
    const auto curr = deep_book(99, 500, 200, 500);
    auto fills = m.on_snapshot(prev, curr, /*now=*/42);

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].id, r.id);
    EXPECT_EQ(fills[0].price, 100);   // resting limit price
    EXPECT_EQ(fills[0].qty, 5);
    EXPECT_EQ(fills[0].ts, 42);
    EXPECT_FALSE(m.has_order(r.id));
}

TEST(Matcher, OnSnapshotDoesNotFillWhenVolumeMerelyDropped) {
    bt::PessimisticQueueModel qm;
    bt::Matcher m(qm);
    const auto prev = deep_book(100, 500, 200, 500);
    auto r = m.submit(bt::Side::Buy, 100, 5, prev, 0);
    ASSERT_TRUE(r.accepted);

    // Volume drop, level still present — no fill, order still resting.
    const auto curr = deep_book(100, 50, 200, 500);
    auto fills = m.on_snapshot(prev, curr, 1);
    EXPECT_TRUE(fills.empty());
    EXPECT_TRUE(m.has_order(r.id));
}

}  // namespace
