#include "bt/order.hpp"
#include "bt/stats.hpp"
#include "bt/types.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>

namespace {

constexpr bt::InstrumentSpec kSpec{1e-7, 1.0};

bt::Fill make_fill(bt::OrderId id, bt::Timestamp ts, bt::Price px, bt::Qty q, bt::Side s) {
    return bt::Fill{ id, ts, px, q, s };
}

TEST(Stats, EmptyStateHasZeroEverything) {
    bt::Stats s;
    EXPECT_EQ(s.submitted_count(), 0u);
    EXPECT_EQ(s.rejected_count(),  0u);
    EXPECT_EQ(s.fill_count(),      0u);
    EXPECT_EQ(s.total_volume(),    0);
    EXPECT_EQ(s.gross_pnl_ticks(), 0);
    EXPECT_EQ(s.max_drawdown_ticks(), 0);
    EXPECT_DOUBLE_EQ(s.reject_rate(), 0.0);
    EXPECT_TRUE(s.equity_curve().empty());
}

TEST(Stats, SubmitAndRejectCounts) {
    bt::Stats s;
    s.on_submit_attempt();
    s.on_submit_attempt();
    s.on_submit_attempt();
    s.on_submit_attempt();
    s.on_reject();
    EXPECT_EQ(s.submitted_count(), 4u);
    EXPECT_EQ(s.rejected_count(),  1u);
    EXPECT_DOUBLE_EQ(s.reject_rate(), 0.25);
}

TEST(Stats, FillCountsVolumeAndGrossPnL) {
    bt::Stats s;
    // Buy 5 @ 100 ticks (cash out 500), Sell 5 @ 110 (cash in 550) → +50.
    s.on_fill(make_fill(1, 0, 100, 5, bt::Side::Buy));
    s.on_fill(make_fill(1, 0, 110, 5, bt::Side::Sell));
    EXPECT_EQ(s.fill_count(),      2u);
    EXPECT_EQ(s.total_volume(),    10);
    EXPECT_EQ(s.gross_pnl_ticks(), 50);
}

TEST(Stats, EquityCurveAndDrawdown) {
    bt::Stats s;
    // Equity: 0, +10, +5, +20, -3, +15
    // Peak rolls: 0,10,10,20,20,20.  DDs: 0,0,5,0,23,5  → max_drawdown=23.
    s.on_mark(100, 0);
    s.on_mark(200, 10);
    s.on_mark(300, 5);
    s.on_mark(400, 20);
    s.on_mark(500, -3);
    s.on_mark(600, 15);
    EXPECT_EQ(s.equity_curve().size(), 6u);
    EXPECT_EQ(s.max_drawdown_ticks(),  23);
}

TEST(Stats, EquityCsvHasHeaderAndAllSamples) {
    bt::Stats s;
    s.on_mark(100, 0);
    s.on_mark(200, 12345);   // 12345 ticks * 1e-7 = 0.0012345

    std::ostringstream os;
    s.write_equity_csv(os, kSpec);
    const std::string out = os.str();

    EXPECT_NE(out.find("ts_us,equity\n"),         std::string::npos);
    EXPECT_NE(out.find("100,"),                   std::string::npos);
    EXPECT_NE(out.find("200,"),                   std::string::npos);
    // Decimal value is present (loose substring check, format-tolerant).
    EXPECT_NE(out.find("0.0012345"),              std::string::npos);
}

TEST(Stats, SummaryContainsKeyFields) {
    bt::Stats s;
    s.on_submit_attempt();
    s.on_submit_attempt();
    s.on_reject();
    s.on_fill(make_fill(1, 0, 100, 5, bt::Side::Buy));
    s.on_mark(0, 1000);     // peak
    s.on_mark(1000, 250);   // drawdown of 750 from peak

    std::ostringstream os;
    s.write_summary(os, kSpec);
    const std::string out = os.str();

    EXPECT_NE(out.find("submitted_orders:   2"),  std::string::npos);
    EXPECT_NE(out.find("rejected_orders:    1"),  std::string::npos);
    EXPECT_NE(out.find("reject_rate:        50"), std::string::npos);
    EXPECT_NE(out.find("fill_count:         1"),  std::string::npos);
    EXPECT_NE(out.find("max_drawdown_ticks: 750"),std::string::npos);
    EXPECT_NE(out.find("final_equity_ticks: 250"),std::string::npos);
    EXPECT_NE(out.find("tick_size:"),             std::string::npos);
}

}  // namespace
