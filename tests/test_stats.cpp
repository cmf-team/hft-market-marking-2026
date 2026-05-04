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
    s.set_sample_interval(0);  // record every mark
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

TEST(Stats, EquityCurveDownsamplesByInterval) {
    bt::Stats s;
    s.set_sample_interval(1000);  // 1ms
    // Marks every 200us. First always recorded; subsequent only when
    // 1000us has elapsed since last recorded sample.
    // ts:  0  200  400  600  800 1000 1200 1400 1600 1800 2000
    // rec: ✓                              ✓                ✓
    for (int i = 0; i <= 10; ++i) {
        s.on_mark(static_cast<bt::Timestamp>(i * 200), i);
    }
    ASSERT_EQ(s.equity_curve().size(), 3u);
    EXPECT_EQ(s.equity_curve()[0].ts, 0);
    EXPECT_EQ(s.equity_curve()[1].ts, 1000);
    EXPECT_EQ(s.equity_curve()[2].ts, 2000);
}

TEST(Stats, DrawdownCapturesIntraIntervalSpike) {
    bt::Stats s;
    s.set_sample_interval(1'000'000);  // huge — only first mark recorded
    s.on_mark(0,        100);   // peak
    s.on_mark(10,        20);   // dip — between samples
    s.on_mark(20,        90);
    // Only one equity point recorded, but drawdown still saw the dip.
    EXPECT_EQ(s.equity_curve().size(), 1u);
    EXPECT_EQ(s.max_drawdown_ticks(),  80);
}

TEST(Stats, FirstAndLastMarkTimestampsTracked) {
    bt::Stats s;
    s.set_sample_interval(1'000'000);
    s.on_mark(1722470402982047, 0);
    s.on_mark(1722470403100000, 1);
    s.on_mark(1722470500000000, 2);
    EXPECT_TRUE(s.has_marks());
    EXPECT_EQ(s.first_mark_ts(), 1722470402982047);
    EXPECT_EQ(s.last_mark_ts(),  1722470500000000);
}

TEST(Stats, EquityCsvHasHeaderAndAllSamples) {
    bt::Stats s;
    s.set_sample_interval(0);  // record every mark
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
    s.set_sample_interval(0);
    s.on_mark(1722470402982047, 1000);   // peak  → 2024-08-01T00:00:02.982047Z
    s.on_mark(1722470403982047, 250);    // drawdown of 750 from peak, +1s later

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
    EXPECT_NE(out.find("start_time_utc:"),        std::string::npos);
    EXPECT_NE(out.find("2024-08-01T00:00:02.982047Z"), std::string::npos);
    EXPECT_NE(out.find("end_time_utc:"),          std::string::npos);
}

}  // namespace
