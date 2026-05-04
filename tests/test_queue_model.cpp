#include "bt/queue_model.hpp"

#include "bt/events.hpp"
#include "bt/order.hpp"
#include "bt/order_book.hpp"
#include "bt/types.hpp"

#include <gtest/gtest.h>

#include <cstddef>

namespace {

// Helper: build a book where bids[0..n_bid-1] and asks[0..n_ask-1] are
// populated from explicit price/amount lists.
struct LevelSpec { bt::Price price; bt::Qty amount; };

bt::BookSnapshot make_snapshot(bt::Timestamp ts,
                               std::initializer_list<LevelSpec> bids,
                               std::initializer_list<LevelSpec> asks) {
    bt::BookSnapshot s{};
    s.ts = ts;
    std::size_t i = 0;
    for (const auto& l : bids) {
        if (i >= bt::kMaxLevels) break;
        s.bids[i++] = { l.price, l.amount };
    }
    i = 0;
    for (const auto& l : asks) {
        if (i >= bt::kMaxLevels) break;
        s.asks[i++] = { l.price, l.amount };
    }
    return s;
}

bt::OrderBook make_book(std::initializer_list<LevelSpec> bids,
                        std::initializer_list<LevelSpec> asks,
                        bt::Timestamp ts = 0) {
    bt::OrderBook b;
    b.apply(make_snapshot(ts, bids, asks));
    return b;
}

bt::Order make_order(bt::Side side, bt::Price price, bt::Qty qty) {
    bt::Order o{};
    o.id = 1;
    o.side = side;
    o.price = price;
    o.qty = qty;
    o.filled = 0;
    o.queue_ahead = 0;
    return o;
}

// --------------------------------------------------------------------------
// initial_queue
// --------------------------------------------------------------------------

TEST(PessimisticQueueModel, InitialQueueIsVolumeAtPrice) {
    bt::PessimisticQueueModel m;
    const auto book = make_book(
        {{100, 500}, {99, 200}, {98, 150}},
        {{101, 300}});

    EXPECT_EQ(m.initial_queue(bt::Side::Buy, 100, book), 500);
    EXPECT_EQ(m.initial_queue(bt::Side::Buy, 99,  book), 200);
    EXPECT_EQ(m.initial_queue(bt::Side::Buy, 98,  book), 150);
    EXPECT_EQ(m.initial_queue(bt::Side::Sell, 101, book), 300);
}

TEST(PessimisticQueueModel, InitialQueueIsZeroForPriceNotInBook) {
    bt::PessimisticQueueModel m;
    const auto book = make_book({{100, 500}}, {{200, 500}});

    // Inside the spread.
    EXPECT_EQ(m.initial_queue(bt::Side::Buy, 150, book), 0);
    // Far from the book.
    EXPECT_EQ(m.initial_queue(bt::Side::Buy, 1, book), 0);
    EXPECT_EQ(m.initial_queue(bt::Side::Sell, 1000, book), 0);
}

TEST(PessimisticQueueModel, InitialQueueOnEmptyBookIsZero) {
    bt::PessimisticQueueModel m;
    const bt::OrderBook empty;
    EXPECT_EQ(m.initial_queue(bt::Side::Buy, 100, empty), 0);
}

// --------------------------------------------------------------------------
// on_trade — queue erosion
// --------------------------------------------------------------------------

TEST(PessimisticQueueModel, OnTradePartiallyErodesQueue) {
    bt::PessimisticQueueModel m;
    auto o = make_order(bt::Side::Buy, 100, 5);
    o.queue_ahead = 10;

    // Trade volume 4 — fully consumed by queue, 0 leftover.
    const bt::Qty leftover = m.on_trade(o, /*available=*/4);
    EXPECT_EQ(leftover, 0);
    EXPECT_EQ(o.queue_ahead, 6);
    EXPECT_EQ(o.filled, 0);  // model never touches `filled`
}

TEST(PessimisticQueueModel, OnTradeClearsQueueExactly) {
    bt::PessimisticQueueModel m;
    auto o = make_order(bt::Side::Buy, 100, 5);
    o.queue_ahead = 10;

    const bt::Qty leftover = m.on_trade(o, /*available=*/10);
    EXPECT_EQ(leftover, 0);
    EXPECT_EQ(o.queue_ahead, 0);
}

TEST(PessimisticQueueModel, OnTradeQueueClearsAndLeavesLeftover) {
    bt::PessimisticQueueModel m;
    auto o = make_order(bt::Side::Buy, 100, 5);
    o.queue_ahead = 7;

    const bt::Qty leftover = m.on_trade(o, /*available=*/12);
    EXPECT_EQ(leftover, 5);  // 12 - 7 = 5
    EXPECT_EQ(o.queue_ahead, 0);
}

TEST(PessimisticQueueModel, OnTradeWithZeroQueueIsPassthrough) {
    bt::PessimisticQueueModel m;
    auto o = make_order(bt::Side::Buy, 100, 5);
    // queue_ahead == 0

    const bt::Qty leftover = m.on_trade(o, /*available=*/8);
    EXPECT_EQ(leftover, 8);
    EXPECT_EQ(o.queue_ahead, 0);
}

// --------------------------------------------------------------------------
// on_snapshot — cases A, B, C, D
// --------------------------------------------------------------------------

TEST(PessimisticQueueModel, OnSnapshotCaseAFillsWhenLevelDisappears) {
    bt::PessimisticQueueModel m;
    auto o = make_order(bt::Side::Buy, 100, 5);
    o.queue_ahead = 10;

    const auto prev = make_book({{100, 500}}, {{200, 500}});
    // New snapshot: bid level 100 is gone, best bid is now 99.
    const auto curr = make_book({{99, 500}}, {{200, 500}});

    const bt::Qty fill = m.on_snapshot(o, prev, curr);
    EXPECT_EQ(fill, 5);  // remaining qty of the order
}

TEST(PessimisticQueueModel, OnSnapshotCaseAOnPartiallyFilledOrder) {
    bt::PessimisticQueueModel m;
    auto o = make_order(bt::Side::Buy, 100, 5);
    o.filled = 2;  // already partially filled
    o.queue_ahead = 10;

    const auto prev = make_book({{100, 500}}, {{200, 500}});
    const auto curr = make_book({{99, 500}},  {{200, 500}});

    const bt::Qty fill = m.on_snapshot(o, prev, curr);
    EXPECT_EQ(fill, 3);  // qty - filled = 5 - 2
}

TEST(PessimisticQueueModel, OnSnapshotCaseBVolumeDroppedNoFillQueueUnchanged) {
    bt::PessimisticQueueModel m;
    auto o = make_order(bt::Side::Buy, 100, 5);
    o.queue_ahead = 100;

    // Volume at 100 dropped from 500 to 200 — but level still present.
    const auto prev = make_book({{100, 500}}, {{200, 500}});
    const auto curr = make_book({{100, 200}}, {{200, 500}});

    const bt::Qty fill = m.on_snapshot(o, prev, curr);
    EXPECT_EQ(fill, 0);
    EXPECT_EQ(o.queue_ahead, 100);  // pessimistic: cancellations come from behind
}

TEST(PessimisticQueueModel, OnSnapshotCaseCVolumeIncreasedQueueUnchanged) {
    bt::PessimisticQueueModel m;
    auto o = make_order(bt::Side::Buy, 100, 5);
    o.queue_ahead = 100;

    const auto prev = make_book({{100, 500}}, {{200, 500}});
    const auto curr = make_book({{100, 800}}, {{200, 500}});

    const bt::Qty fill = m.on_snapshot(o, prev, curr);
    EXPECT_EQ(fill, 0);
    EXPECT_EQ(o.queue_ahead, 100);
}

TEST(PessimisticQueueModel, OnSnapshotCaseDUnchangedNoOp) {
    bt::PessimisticQueueModel m;
    auto o = make_order(bt::Side::Buy, 100, 5);
    o.queue_ahead = 100;

    const auto prev = make_book({{100, 500}}, {{200, 500}});
    const auto curr = make_book({{100, 500}}, {{200, 500}});

    const bt::Qty fill = m.on_snapshot(o, prev, curr);
    EXPECT_EQ(fill, 0);
    EXPECT_EQ(o.queue_ahead, 100);
}

TEST(PessimisticQueueModel, OnSnapshotCaseAOnAskSide) {
    bt::PessimisticQueueModel m;
    auto o = make_order(bt::Side::Sell, 200, 5);
    o.queue_ahead = 10;

    const auto prev = make_book({{100, 500}}, {{200, 500}});
    // Ask 200 disappears, new best ask is 201.
    const auto curr = make_book({{100, 500}}, {{201, 500}});

    const bt::Qty fill = m.on_snapshot(o, prev, curr);
    EXPECT_EQ(fill, 5);
}

}  // namespace
