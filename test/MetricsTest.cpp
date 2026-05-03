#include "engine/Metrics.hpp"

#include "catch2/catch_all.hpp"

using namespace cmf;

static std::vector<TradeRecord> make_trades(
    const std::vector<std::tuple<NanoTime, Side, double, double, double>>& data)
{
    std::vector<TradeRecord> v;
    for (const auto& [ts, side, qty, price, fee] : data)
        v.push_back({ts, qty, price, side, fee});
    return v;
}

TEST_CASE("Turnover sums price*qty", "[Metrics]")
{
    auto trades = make_trades({
        {1, Side::Buy,  10.0, 100.0, 0.0},
        {2, Side::Sell, 5.0,  102.0, 0.0},
    });
    Turnover t(trades);
    REQUIRE(t.calculate() == Catch::Approx(10.0 * 100.0 + 5.0 * 102.0));
}

TEST_CASE("Turnover - empty returns 0", "[Metrics]")
{
    std::vector<TradeRecord> empty;
    Turnover t(empty);
    REQUIRE(t.calculate() == 0.0);
}

TEST_CASE("MaxAbsInventory - positive and negative", "[Metrics]")
{
    std::vector<double> inv = {0.0, 3.0, -5.0, 2.0, -1.0};
    MaxAbsInventory m(inv);
    REQUIRE(m.calculate() == Catch::Approx(5.0));
}

TEST_CASE("MaxAbsInventory - empty returns 0", "[Metrics]")
{
    std::vector<double> empty;
    MaxAbsInventory m(empty);
    REQUIRE(m.calculate() == 0.0);
}

TEST_CASE("TimeWeightedAvgInventory - uniform steps", "[Metrics]")
{
    std::vector<double> inv = {2.0, 4.0, 6.0};
    std::vector<NanoTime> ts = {0, 1000, 2000};
    TimeWeightedAvgInventory m(inv, ts);
    // Two intervals: [0,1000) with inv=2.0, [1000,2000) with inv=4.0
    // TWAP = (2.0*1000 + 4.0*1000) / 2000 = 3.0
    REQUIRE(m.calculate() == Catch::Approx(3.0));
}

TEST_CASE("FillRatio - correct fraction", "[Metrics]")
{
    FillRatio f(10, 7);
    REQUIRE(f.calculate() == Catch::Approx(0.7));
}

TEST_CASE("FillRatio - zero sent returns 0", "[Metrics]")
{
    FillRatio f(0, 0);
    REQUIRE(f.calculate() == 0.0);
}

TEST_CASE("MaxDrawdownPct - simple drawdown", "[Metrics]")
{
    // Peak 100, then drops to 80 -> drawdown = (100-80)/100*100 = 20.0
    std::vector<double> eq = {100.0, 90.0, 80.0, 95.0};
    MaxDrawdownPct m(eq);
    REQUIRE(m.calculate() == Catch::Approx(20.0).epsilon(0.01));
}

TEST_CASE("MaxDrawdownPct - no drawdown", "[Metrics]")
{
    std::vector<double> eq = {100.0, 110.0, 120.0};
    MaxDrawdownPct m(eq);
    REQUIRE(m.calculate() == Catch::Approx(0.0));
}

TEST_CASE("RealizedPnL - round-trip buy then sell", "[Metrics]")
{
    // Buy 1 unit at 100, sell 1 unit at 102 -> realized = +2 (minus fees)
    auto trades = make_trades({
        {1, Side::Buy,  1.0, 100.0, 0.0},
        {2, Side::Sell, 1.0, 102.0, 0.0},
    });
    RealizedPnL p(trades);
    REQUIRE(p.calculate() == Catch::Approx(2.0));
}
