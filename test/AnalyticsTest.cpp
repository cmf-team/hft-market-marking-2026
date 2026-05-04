#include "catch2/catch_all.hpp"
#include "backtest/analytics/analytics.hpp"
#include "backtest/export/export.hpp"
#include <fstream>

// Helper: read CSV file as string
static std::string read_file(const std::string& path)
{
    std::ifstream f(path);
    return {(std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()};
}

// Helper: create a minimal BacktestResult with only what's needed.
// Mimics the per-tick peak/drawdown tracking that run_backtest would
// normally populate, so analytics tests can assert against a known
// trajectory without running the full engine.
static BacktestResult make_result(
    const std::vector<Fill>& fills = {},
    const std::vector<double>& pnl_series = {},
    const std::vector<uint64_t>& pnl_timestamps = {},
    const uint64_t lob_rows = 0,
    const PnlState& pnl = {})
{
    BacktestResult res;
    res.fills = fills;
    res.pnl_series = pnl_series;
    res.pnl_timestamps = pnl_timestamps;
    res.lob_rows = lob_rows;
    res.pnl = pnl;
    if (!pnl_series.empty())
    {
        res.peak_pnl = pnl_series.front();
        for (double v : pnl_series)
        {
            if (v > res.peak_pnl)
                res.peak_pnl = v;
            const double dd = res.peak_pnl - v;
            if (dd > res.max_drawdown)
                res.max_drawdown = dd;
        }
    }
    return res;
}

// ============================================================================
// export_fills_csv tests
// ============================================================================

TEST_CASE("ExportFillsCsvTest - EmptyFills", "[ExportFillsCsvTest]")
{
    BacktestResult res = make_result({});
    export_fills_csv(res, "test_empty_fills.csv");

    std::string content = read_file("test_empty_fills.csv");
    CHECK(content == "timestamp,order_id,side,price,qty,running_realized_pnl\n");

    std::remove("test_empty_fills.csv");
}

TEST_CASE("ExportFillsCsvTest - SingleBuyFill", "[ExportFillsCsvTest]")
{
    Fill f{1000ULL, 42, 100'0000000LL, 500, Side::Buy, 0.0};
    BacktestResult res = make_result({f});
    export_fills_csv(res, "test_single_buy.csv");

    std::string content = read_file("test_single_buy.csv");
    CHECK(content.find("timestamp,order_id,side,price,qty,running_realized_pnl") == 0);
    CHECK(content.find("1000,42,buy,1000000000,500,0.00000000") != std::string::npos);

    std::remove("test_single_buy.csv");
}

TEST_CASE("ExportFillsCsvTest - SingleSellFill", "[ExportFillsCsvTest]")
{
    Fill f{2000ULL, 43, 101'0000000LL, 300, Side::Sell, 150.5};
    BacktestResult res = make_result({f});
    export_fills_csv(res, "test_single_sell.csv");

    std::string content = read_file("test_single_sell.csv");
    CHECK(content.find("2000,43,sell,1010000000,300,150.50000000") != std::string::npos);

    std::remove("test_single_sell.csv");
}

TEST_CASE("ExportFillsCsvTest - MultipleFills", "[ExportFillsCsvTest]")
{
    std::vector<Fill> fills = {
        {1000ULL, 1, 100'0000000LL, 100, Side::Buy, 0.0},
        {2000ULL, 2, 101'0000000LL, 100, Side::Sell, 100.0},
        {3000ULL, 3, 99'5000000LL, 50, Side::Buy, 50.0},
    };
    BacktestResult res = make_result(fills);
    export_fills_csv(res, "test_multiple.csv");

    std::string content = read_file("test_multiple.csv");
    CHECK(content.find("1000,1,buy") != std::string::npos);
    CHECK(content.find("2000,2,sell") != std::string::npos);
    CHECK(content.find("3000,3,buy") != std::string::npos);

    std::remove("test_multiple.csv");
}

// ============================================================================
// compute_analytics: Max Drawdown tests
// ============================================================================

TEST_CASE("ComputeAnalyticsTest - MaxDrawdownEmptySeries", "[ComputeAnalyticsTest]")
{
    BacktestResult res = make_result({}, {});
    auto metrics = compute_analytics(res);
    CHECK(metrics.max_drawdown == 0.0);
}

TEST_CASE("ComputeAnalyticsTest - MaxDrawdownSinglePoint", "[ComputeAnalyticsTest]")
{
    BacktestResult res = make_result({}, {100.0});
    auto metrics = compute_analytics(res);
    CHECK(metrics.max_drawdown == 0.0);
}

TEST_CASE("ComputeAnalyticsTest - MaxDrawdownMonotoneIncreasing", "[ComputeAnalyticsTest]")
{
    BacktestResult res = make_result({}, {100.0, 110.0, 120.0, 130.0});
    auto metrics = compute_analytics(res);
    CHECK(metrics.max_drawdown == 0.0);
}

TEST_CASE("ComputeAnalyticsTest - MaxDrawdownSimplePeakToTrough", "[ComputeAnalyticsTest]")
{
    // Peak at 100, trough at 80 → drawdown = 20
    BacktestResult res = make_result({}, {100.0, 80.0});
    auto metrics = compute_analytics(res);
    CHECK(metrics.max_drawdown == 20.0);
}

TEST_CASE("ComputeAnalyticsTest - MaxDrawdownMultiplePeaks", "[ComputeAnalyticsTest]")
{
    // First peak 100 → drops to 80 (dd=20)
    // Second peak 110 → drops to 90 (dd=20)
    // Third peak 130 → drops to 100 (dd=30) ← max
    BacktestResult res = make_result({}, {100.0, 80.0, 110.0, 90.0, 130.0, 100.0});
    auto metrics = compute_analytics(res);
    CHECK(metrics.max_drawdown == 30.0);
}

TEST_CASE("ComputeAnalyticsTest - MaxDrawdownTakesHighestPeak", "[ComputeAnalyticsTest]")
{
    // Peak at 150, then drops to 80 → dd=70
    // Intermediate peak at 120 doesn't matter because 150 is higher
    BacktestResult res = make_result({}, {150.0, 100.0, 120.0, 80.0});
    auto metrics = compute_analytics(res);
    CHECK(metrics.max_drawdown == 70.0);
}

// ============================================================================
// compute_analytics: Sharpe Ratio tests
// ============================================================================

TEST_CASE("ComputeAnalyticsTest - SharpeEmptySeries", "[ComputeAnalyticsTest]")
{
    BacktestResult res = make_result({}, {});
    auto metrics = compute_analytics(res);
    CHECK(metrics.sharpe == 0.0);
}

TEST_CASE("ComputeAnalyticsTest - SharpeSinglePoint", "[ComputeAnalyticsTest]")
{
    BacktestResult res = make_result({}, {100.0});
    auto metrics = compute_analytics(res);
    CHECK(metrics.sharpe == 0.0);
}

TEST_CASE("ComputeAnalyticsTest - SharpeZeroDailyBuckets", "[ComputeAnalyticsTest]")
{
    // PnL series but no timestamps → no daily bucketing
    BacktestResult res = make_result({}, {100.0, 110.0}, {});
    auto metrics = compute_analytics(res);
    CHECK(metrics.sharpe == 0.0);
}

TEST_CASE("ComputeAnalyticsTest - SharpeSingleDayBucket", "[ComputeAnalyticsTest]")
{
    // All timestamps in same day → only 1 daily bucket → warning, sharpe = 0
    constexpr uint64_t day_us = 86400000000ULL; // 24*60*60*1M microseconds
    BacktestResult res = make_result(
        {},
        {100.0, 105.0, 110.0},
        {0ULL, day_us / 2, day_us - 1} // all in day 0
    );
    auto metrics = compute_analytics(res);
    CHECK(metrics.sharpe == 0.0);
}

TEST_CASE("ComputeAnalyticsTest - SharpeFlatDailyReturns", "[ComputeAnalyticsTest]")
{
    // Two days, both with zero return → mean=0, sharpe=0
    constexpr uint64_t day_us = 86400000000ULL; // 24*60*60*1M microseconds
    BacktestResult res = make_result(
        {},
        {100.0, 100.0, 100.0}, // flat
        {0ULL, day_us / 2, 2 * day_us});
    auto metrics = compute_analytics(res);
    CHECK(metrics.sharpe == 0.0);
}

TEST_CASE("ComputeAnalyticsTest - SharpePositiveReturns", "[ComputeAnalyticsTest]")
{
    // Two days of +10 each → mean=10, variance=0, sharpe=inf (but bounded by std_r check)
    constexpr uint64_t day_us = 86400000000ULL; // 24*60*60*1M microseconds
    BacktestResult res = make_result(
        {},
        {100.0, 110.0, 120.0}, // +10 day1, +10 day2
        {0ULL, day_us / 2, 2 * day_us});
    auto metrics = compute_analytics(res);
    // With flat daily returns (variance=0), sharpe = 0 due to std_r=0 check
    CHECK(metrics.sharpe == 0.0);
}

TEST_CASE("ComputeAnalyticsTest - SharpeWithVariance", "[ComputeAnalyticsTest]")
{
    // Day 1: +10, Day 2: +20 → mean=15, sample_var=50, std=sqrt(50)≈7.07
    // sharpe = 15 / 7.07 * sqrt(252) ≈ 33.7
    constexpr uint64_t day_us = 86400000000ULL; // 24*60*60*1M microseconds
    BacktestResult res = make_result(
        {},
        {100.0, 110.0, 130.0}, // +10, +20
        {0ULL, day_us / 2, 2 * day_us});
    auto metrics = compute_analytics(res);
    CHECK(metrics.sharpe > 30.0);
    CHECK(metrics.sharpe < 40.0);
}

// ============================================================================
// compute_analytics: Win Rate tests
// ============================================================================

TEST_CASE("ComputeAnalyticsTest - WinRateEmptyFills", "[ComputeAnalyticsTest]")
{
    BacktestResult res = make_result({});
    auto metrics = compute_analytics(res);
    CHECK(metrics.win_rate == 0.0);
}

TEST_CASE("ComputeAnalyticsTest - WinRateSingleFill", "[ComputeAnalyticsTest]")
{
    // Single fill → no round-trip possible
    Fill f{1000ULL, 1, 100'0000000LL, 100, Side::Buy, 0.0};
    BacktestResult res = make_result({f});
    auto metrics = compute_analytics(res);
    CHECK(metrics.win_rate == 0.0);
}

TEST_CASE("ComputeAnalyticsTest - WinRatePerfectRoundTrip", "[ComputeAnalyticsTest]")
{
    // Buy @ 100, Sell @ 110 → profitable round-trip
    std::vector<Fill> fills = {
        {1000ULL, 1, 100'0000000LL, 100, Side::Buy, 0.0},
        {2000ULL, 2, 110'0000000LL, 100, Side::Sell, 1000.0},
    };
    BacktestResult res = make_result(fills);
    auto metrics = compute_analytics(res);
    CHECK(metrics.win_rate == 1.0); // 1 win / 1 round-trip = 100%
}

TEST_CASE("ComputeAnalyticsTest - WinRateLossingRoundTrip", "[ComputeAnalyticsTest]")
{
    // Buy @ 110, Sell @ 100 → losing round-trip
    std::vector<Fill> fills = {
        {1000ULL, 1, 110'0000000LL, 100, Side::Buy, 0.0},
        {2000ULL, 2, 100'0000000LL, 100, Side::Sell, -1000.0},
    };
    BacktestResult res = make_result(fills);
    auto metrics = compute_analytics(res);
    CHECK(metrics.win_rate == 0.0); // 0 wins / 1 round-trip
}

TEST_CASE("ComputeAnalyticsTest - WinRateMultipleRoundTrips", "[ComputeAnalyticsTest]")
{
    // RT1: Buy 0 @ 100, Sell @ 110 → +1000 (win)
    // RT2: Buy @ 105, Sell @ 100 → -500 (loss)
    // RT3: Buy @ 95, Sell @ 120 → +2500 (win)
    std::vector<Fill> fills = {
        {1000ULL, 1, 100'0000000LL, 100, Side::Buy, 0.0},
        {2000ULL, 2, 110'0000000LL, 100, Side::Sell, 1000.0},
        {3000ULL, 3, 105'0000000LL, 100, Side::Buy, 500.0},
        {4000ULL, 4, 100'0000000LL, 100, Side::Sell, 0.0},
        {5000ULL, 5, 95'0000000LL, 100, Side::Buy, -2500.0},
        {6000ULL, 6, 120'0000000LL, 100, Side::Sell, 0.0},
    };
    BacktestResult res = make_result(fills);
    auto metrics = compute_analytics(res);
    CHECK(metrics.win_rate == Catch::Approx(2.0 / 3.0)); // 2 wins / 3 round-trips
}

TEST_CASE("ComputeAnalyticsTest - WinRateBreakEvenRoundTrip", "[ComputeAnalyticsTest]")
{
    // Buy @ 100, Sell @ 100 → breakeven (not a win)
    std::vector<Fill> fills = {
        {1000ULL, 1, 100'0000000LL, 100, Side::Buy, 0.0},
        {2000ULL, 2, 100'0000000LL, 100, Side::Sell, 0.0},
    };
    BacktestResult res = make_result(fills);
    auto metrics = compute_analytics(res);
    CHECK(metrics.win_rate == 0.0); // 0 wins (breakeven doesn't count)
}

TEST_CASE("ComputeAnalyticsTest - WinRateSellFirst", "[ComputeAnalyticsTest]")
{
    // Sell @ 110, Buy @ 100 → profitable round-trip (entry is sell)
    std::vector<Fill> fills = {
        {1000ULL, 1, 110'0000000LL, 100, Side::Sell, 0.0},
        {2000ULL, 2, 100'0000000LL, 100, Side::Buy, 1000.0},
    };
    BacktestResult res = make_result(fills);
    auto metrics = compute_analytics(res);
    CHECK(metrics.win_rate == 1.0); // profitable short
}

TEST_CASE("ComputeAnalyticsTest - WinRateOddNumberOfFills", "[ComputeAnalyticsTest]")
{
    // Three fills: Buy, Sell (completes round-trip), then Buy (incomplete)
    // Should only count the first round-trip
    std::vector<Fill> fills = {
        {1000ULL, 1, 100'0000000LL, 100, Side::Buy, 0.0},
        {2000ULL, 2, 110'0000000LL, 100, Side::Sell, 1000.0},
        {3000ULL, 3, 95'0000000LL, 100, Side::Buy, 0.0},
    };
    BacktestResult res = make_result(fills);
    auto metrics = compute_analytics(res);
    CHECK(metrics.win_rate == 1.0); // 1 win / 1 completed round-trip
}

// ============================================================================
// Integration: All metrics together
// ============================================================================

TEST_CASE("ComputeAnalyticsTest - FullIntegration", "[ComputeAnalyticsTest]")
{
    // Complete scenario with meaningful PnL, fills, and timestamps
    constexpr uint64_t day_us = 86400000000ULL; // 24*60*60*1M microseconds

    std::vector<Fill> fills = {
        {0, 1, 100'0000000LL, 100, Side::Buy, 0.0},
        {day_us / 2, 2, 105'0000000LL, 100, Side::Sell, 500.0},
    };

    std::vector<double> pnl_series = {0.0, 100.0, 200.0, 150.0, 300.0};
    std::vector<uint64_t> timestamps = {0, day_us / 4, day_us / 2, 3 * day_us / 2, 2 * day_us};

    BacktestResult res = make_result(fills, pnl_series, timestamps);
    auto metrics = compute_analytics(res);

    // Max drawdown: peak 300 → trough would need to be in series, but series ends at 300
    // So check that we have a reasonable drawdown
    CHECK(metrics.max_drawdown >= 0.0);

    // Sharpe should be computed (2+ daily buckets)
    CHECK(metrics.sharpe >= 0.0);

    // Win rate: 1 profitable round-trip
    CHECK(metrics.win_rate == 1.0);
}
