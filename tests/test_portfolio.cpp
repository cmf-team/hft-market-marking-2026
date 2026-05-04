#include "bt/portfolio.hpp"

#include "bt/order.hpp"
#include "bt/types.hpp"

#include <gtest/gtest.h>

namespace {

bt::Fill buy(bt::Price price, bt::Qty qty) {
    return bt::Fill{ /*id=*/0, /*ts=*/0, price, qty, bt::Side::Buy };
}
bt::Fill sell(bt::Price price, bt::Qty qty) {
    return bt::Fill{ /*id=*/0, /*ts=*/0, price, qty, bt::Side::Sell };
}

// --------------------------------------------------------------------------
// Opening from flat
// --------------------------------------------------------------------------

TEST(Portfolio, FlatStartHasZeroEverything) {
    bt::Portfolio p;
    EXPECT_EQ(p.position(), 0);
    EXPECT_EQ(p.avg_entry_price(), 0);
    EXPECT_EQ(p.realized_pnl_ticks(), 0);
    EXPECT_EQ(p.unrealized_pnl_ticks(), 0);
}

TEST(Portfolio, OpenLongFromFlat) {
    bt::Portfolio p;
    p.on_fill(buy(/*price=*/100, /*qty=*/5));
    EXPECT_EQ(p.position(), 5);
    EXPECT_EQ(p.avg_entry_price(), 100);
    EXPECT_EQ(p.realized_pnl_ticks(), 0);
}

TEST(Portfolio, OpenShortFromFlat) {
    bt::Portfolio p;
    p.on_fill(sell(/*price=*/100, /*qty=*/5));
    EXPECT_EQ(p.position(), -5);
    EXPECT_EQ(p.avg_entry_price(), 100);
    EXPECT_EQ(p.realized_pnl_ticks(), 0);
}

// --------------------------------------------------------------------------
// Adding to position — weighted-average roll
// --------------------------------------------------------------------------

TEST(Portfolio, AddingToLongRollsWeightedAverage) {
    bt::Portfolio p;
    p.on_fill(buy(100, 10));   // avg = 100
    p.on_fill(buy(110, 10));   // avg = (100*10 + 110*10) / 20 = 105
    EXPECT_EQ(p.position(), 20);
    EXPECT_EQ(p.avg_entry_price(), 105);
    EXPECT_EQ(p.realized_pnl_ticks(), 0);
}

TEST(Portfolio, AddingToShortRollsWeightedAverage) {
    bt::Portfolio p;
    p.on_fill(sell(200, 10));  // avg = 200
    p.on_fill(sell(220, 30));  // avg = (200*10 + 220*30) / 40 = 215
    EXPECT_EQ(p.position(), -40);
    EXPECT_EQ(p.avg_entry_price(), 215);
    EXPECT_EQ(p.realized_pnl_ticks(), 0);
}

// --------------------------------------------------------------------------
// Closing — realized PnL
// --------------------------------------------------------------------------

TEST(Portfolio, PartialCloseOfLongRealizesPnLAndKeepsAvgEntry) {
    bt::Portfolio p;
    p.on_fill(buy(100, 10));      // long 10 @ 100
    p.on_fill(sell(105, 4));      // close 4 @ 105
    EXPECT_EQ(p.position(), 6);
    EXPECT_EQ(p.avg_entry_price(), 100);  // unchanged on close
    EXPECT_EQ(p.realized_pnl_ticks(), (105 - 100) * 4);  // +20
}

TEST(Portfolio, PartialCloseOfShortRealizesPnL) {
    bt::Portfolio p;
    p.on_fill(sell(100, 10));     // short 10 @ 100
    p.on_fill(buy(95, 3));        // cover 3 @ 95
    EXPECT_EQ(p.position(), -7);
    EXPECT_EQ(p.avg_entry_price(), 100);
    EXPECT_EQ(p.realized_pnl_ticks(), (100 - 95) * 3);  // +15
}

TEST(Portfolio, FullCloseFlattensPositionAndZerosAvgEntry) {
    bt::Portfolio p;
    p.on_fill(buy(100, 5));
    p.on_fill(sell(110, 5));
    EXPECT_EQ(p.position(), 0);
    EXPECT_EQ(p.avg_entry_price(), 0);
    EXPECT_EQ(p.realized_pnl_ticks(), (110 - 100) * 5);  // +50
}

TEST(Portfolio, LosingTradeRealizesNegativePnL) {
    bt::Portfolio p;
    p.on_fill(buy(100, 5));
    p.on_fill(sell(90, 5));
    EXPECT_EQ(p.realized_pnl_ticks(), -50);
}

// --------------------------------------------------------------------------
// Position flip
// --------------------------------------------------------------------------

TEST(Portfolio, FlipLongToShortInOneFill) {
    bt::Portfolio p;
    p.on_fill(buy(100, 5));        // long 5 @ 100
    p.on_fill(sell(110, 8));       // sell 8: close 5 @ 110, then open short 3 @ 110
    EXPECT_EQ(p.position(), -3);
    EXPECT_EQ(p.avg_entry_price(), 110);
    EXPECT_EQ(p.realized_pnl_ticks(), (110 - 100) * 5);  // +50 from the closed long
}

TEST(Portfolio, FlipShortToLongInOneFill) {
    bt::Portfolio p;
    p.on_fill(sell(200, 5));       // short 5 @ 200
    p.on_fill(buy(180, 12));       // buy 12: cover 5 @ 180, then open long 7 @ 180
    EXPECT_EQ(p.position(), 7);
    EXPECT_EQ(p.avg_entry_price(), 180);
    EXPECT_EQ(p.realized_pnl_ticks(), (200 - 180) * 5);  // +100 from the closed short
}

// --------------------------------------------------------------------------
// Mark-to-market
// --------------------------------------------------------------------------

TEST(Portfolio, MarkToMarketLong) {
    bt::Portfolio p;
    p.on_fill(buy(100, 5));
    p.mark_to_market(/*mid=*/103);
    EXPECT_EQ(p.unrealized_pnl_ticks(), (103 - 100) * 5);   // +15
    EXPECT_EQ(p.total_pnl_ticks(), 15);
}

TEST(Portfolio, MarkToMarketShort) {
    bt::Portfolio p;
    p.on_fill(sell(100, 5));
    p.mark_to_market(/*mid=*/97);
    EXPECT_EQ(p.unrealized_pnl_ticks(), (97 - 100) * -5);   // +15 (short profited)
}

TEST(Portfolio, MarkToMarketWhenFlatIsZero) {
    bt::Portfolio p;
    p.on_fill(buy(100, 5));
    p.on_fill(sell(110, 5));
    p.mark_to_market(/*mid=*/123);
    EXPECT_EQ(p.unrealized_pnl_ticks(), 0);
    EXPECT_EQ(p.realized_pnl_ticks(), 50);
    EXPECT_EQ(p.total_pnl_ticks(), 50);
}

TEST(Portfolio, MarkToMarketDoesNotTouchRealized) {
    bt::Portfolio p;
    p.on_fill(buy(100, 10));
    p.on_fill(sell(105, 4));        // realized = +20
    p.mark_to_market(/*mid=*/120);  // unrealized only
    EXPECT_EQ(p.realized_pnl_ticks(), 20);
    EXPECT_EQ(p.unrealized_pnl_ticks(), (120 - 100) * 6);  // 6 long @ 100, mark 120 → +120
    EXPECT_EQ(p.total_pnl_ticks(), 140);
}

// --------------------------------------------------------------------------
// Round-trip with returns to flat — PnL math closes exactly
// --------------------------------------------------------------------------

TEST(Portfolio, RoundTripPnLClosesExactly) {
    bt::Portfolio p;
    p.on_fill(buy(100, 5));
    p.on_fill(buy(102, 5));   // long 10 avg 101
    p.on_fill(sell(110, 7));  // close 7 → realized (110-101)*7 = +63, pos 3 @ 101
    p.on_fill(sell(108, 3));  // close 3 → realized (108-101)*3 = +21, pos 0
    EXPECT_EQ(p.position(), 0);
    EXPECT_EQ(p.realized_pnl_ticks(), 63 + 21);  // +84
}

}  // namespace
