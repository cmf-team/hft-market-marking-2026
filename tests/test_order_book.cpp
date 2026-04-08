#include "bt/order_book.hpp"
#include "bt/events.hpp"
#include "bt/types.hpp"

#include <gtest/gtest.h>

#include <cstddef>

namespace {

// Build a deterministic 25-level snapshot. Bids descend from `bid_top`, asks
// ascend from `ask_top`, amounts are unique per (side, depth) so any test
// failure points at exactly which level was wrong.
bt::BookSnapshot make_snapshot(bt::Timestamp ts, bt::Price bid_top, bt::Price ask_top) {
    bt::BookSnapshot s{};
    s.ts = ts;
    for (std::size_t i = 0; i < bt::kMaxLevels; ++i) {
        s.bids[i] = {
            bid_top - static_cast<bt::Price>(i),
            static_cast<bt::Qty>(100 + i * 10),
        };
        s.asks[i] = {
            ask_top + static_cast<bt::Price>(i),
            static_cast<bt::Qty>(200 + i * 20),
        };
    }
    return s;
}

TEST(OrderBook, EmptyBeforeFirstApply) {
    bt::OrderBook book;
    EXPECT_TRUE(book.empty());
    EXPECT_EQ(book.best_bid(), 0);
    EXPECT_EQ(book.best_ask(), 0);
    EXPECT_EQ(book.mid(), 0);
    EXPECT_EQ(book.last_update_ts(), 0);
    EXPECT_EQ(book.volume_at(bt::Side::Buy, 110435), 0);
    EXPECT_EQ(book.volume_at(bt::Side::Sell, 110436), 0);
}

TEST(OrderBook, ApplyExposesTopOfBook) {
    bt::OrderBook book;
    book.apply(make_snapshot(1234, /*bid_top=*/110435, /*ask_top=*/110436));

    EXPECT_FALSE(book.empty());
    EXPECT_EQ(book.best_bid(), 110435);
    EXPECT_EQ(book.best_ask(), 110436);
    EXPECT_EQ(book.mid(), (110435 + 110436) / 2);  // = 110435 (floor)
    EXPECT_EQ(book.last_update_ts(), 1234);
}

TEST(OrderBook, LevelDepthAccess) {
    bt::OrderBook book;
    book.apply(make_snapshot(1, 110435, 110436));

    // Top of book.
    EXPECT_EQ(book.level(bt::Side::Buy,  0).price, 110435);
    EXPECT_EQ(book.level(bt::Side::Sell, 0).price, 110436);
    EXPECT_EQ(book.level(bt::Side::Buy,  0).amount, 100);
    EXPECT_EQ(book.level(bt::Side::Sell, 0).amount, 200);

    // Mid-depth.
    EXPECT_EQ(book.level(bt::Side::Buy,  10).price, 110435 - 10);
    EXPECT_EQ(book.level(bt::Side::Sell, 10).price, 110436 + 10);
    EXPECT_EQ(book.level(bt::Side::Buy,  10).amount, 100 + 10 * 10);
    EXPECT_EQ(book.level(bt::Side::Sell, 10).amount, 200 + 10 * 20);

    // Bottom of book.
    EXPECT_EQ(book.level(bt::Side::Buy,  bt::kMaxLevels - 1).price, 110435 - 24);
    EXPECT_EQ(book.level(bt::Side::Sell, bt::kMaxLevels - 1).price, 110436 + 24);
}

TEST(OrderBook, VolumeAtFindsExistingLevels) {
    bt::OrderBook book;
    book.apply(make_snapshot(1, 110435, 110436));

    // bids[0] amount = 100, bids[5] amount = 150, bids[24] amount = 340
    EXPECT_EQ(book.volume_at(bt::Side::Buy, 110435),     100);
    EXPECT_EQ(book.volume_at(bt::Side::Buy, 110435 - 5), 150);
    EXPECT_EQ(book.volume_at(bt::Side::Buy, 110435 - 24), 340);

    // asks[0] amount = 200, asks[3] amount = 260, asks[24] amount = 680
    EXPECT_EQ(book.volume_at(bt::Side::Sell, 110436),     200);
    EXPECT_EQ(book.volume_at(bt::Side::Sell, 110436 + 3), 260);
    EXPECT_EQ(book.volume_at(bt::Side::Sell, 110436 + 24), 680);
}

TEST(OrderBook, VolumeAtReturnsZeroForUnknownPrice) {
    bt::OrderBook book;
    book.apply(make_snapshot(1, 110435, 110436));

    // A price strictly inside the spread (none — the spread is 1 tick — but
    // a far-away price.)
    EXPECT_EQ(book.volume_at(bt::Side::Buy,  999999), 0);
    EXPECT_EQ(book.volume_at(bt::Side::Sell, 999999), 0);
    // A bid price that doesn't exist on the bid side (e.g. an ask price).
    EXPECT_EQ(book.volume_at(bt::Side::Buy,  110436), 0);
    // And vice versa.
    EXPECT_EQ(book.volume_at(bt::Side::Sell, 110435), 0);
}

TEST(OrderBook, ApplyReplacesPreviousSnapshot) {
    bt::OrderBook book;
    book.apply(make_snapshot(1000, 110435, 110436));

    // Sanity check first snapshot is in place.
    EXPECT_EQ(book.best_bid(), 110435);
    EXPECT_EQ(book.volume_at(bt::Side::Buy, 110435), 100);

    // Apply a totally different snapshot — different prices and amounts.
    bt::BookSnapshot s2{};
    s2.ts = 2000;
    for (std::size_t i = 0; i < bt::kMaxLevels; ++i) {
        s2.bids[i] = { 200000 - static_cast<bt::Price>(i), 999 };
        s2.asks[i] = { 200001 + static_cast<bt::Price>(i), 888 };
    }
    book.apply(s2);

    EXPECT_EQ(book.best_bid(), 200000);
    EXPECT_EQ(book.best_ask(), 200001);
    EXPECT_EQ(book.last_update_ts(), 2000);

    // None of the old prices should still be findable.
    EXPECT_EQ(book.volume_at(bt::Side::Buy,  110435), 0);
    EXPECT_EQ(book.volume_at(bt::Side::Sell, 110436), 0);

    // Every level of the new snapshot should be findable with the new amounts.
    for (std::size_t i = 0; i < bt::kMaxLevels; ++i) {
        EXPECT_EQ(book.volume_at(bt::Side::Buy,  200000 - static_cast<bt::Price>(i)), 999);
        EXPECT_EQ(book.volume_at(bt::Side::Sell, 200001 + static_cast<bt::Price>(i)), 888);
    }
}

TEST(OrderBook, MidIsFloorOfTopOfBookSum) {
    bt::OrderBook book;
    // Even spread: (10 + 20) / 2 = 15 — exact.
    book.apply(make_snapshot(1, 10, 20));
    EXPECT_EQ(book.mid(), 15);

    // Odd spread: (10 + 21) / 2 = 15 — floored, the half-tick is dropped.
    bt::BookSnapshot s{};
    s.ts = 2;
    s.bids[0] = { 10, 1 };
    s.asks[0] = { 21, 1 };
    book.apply(s);
    EXPECT_EQ(book.mid(), 15);
}

}  // namespace
