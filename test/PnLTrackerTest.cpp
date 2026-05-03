// tests for PnLTracker

#include "common/PnLTracker.hpp"
#include "common/Types.hpp"

#include "catch2/catch_all.hpp"

using namespace cmf;

TEST_CASE("PnLTracker - flat round trip realises spread", "[PnLTracker]")
{
    PnLTracker pnl;
    pnl.onFill({1, Side::Buy, 100.0, 5.0, 1});
    pnl.onFill({2, Side::Sell, 101.0, 5.0, 2});

    REQUIRE(pnl.inventory() == Catch::Approx(0.0));
    REQUIRE(pnl.realizedPnl() == Catch::Approx(5.0));
    REQUIRE(pnl.turnover() == Catch::Approx(5 * 100.0 + 5 * 101.0));
    REQUIRE(pnl.totalPnl() == Catch::Approx(5.0));
}

TEST_CASE("PnLTracker - unrealized tracks mid price", "[PnLTracker]")
{
    PnLTracker pnl;
    pnl.onFill({1, Side::Buy, 100.0, 10.0, 1});
    pnl.markToMarket(102.0, 2);

    REQUIRE(pnl.inventory() == Catch::Approx(10.0));
    REQUIRE(pnl.unrealizedPnl() == Catch::Approx(20.0));
    REQUIRE(pnl.totalPnl() == Catch::Approx(20.0));
}

TEST_CASE("PnLTracker - turnover is sum of |qty|*price", "[PnLTracker]")
{
    PnLTracker pnl;
    pnl.onFill({1, Side::Buy, 50.0, 2.0, 1});
    pnl.onFill({2, Side::Sell, 49.0, 1.0, 2});
    pnl.onFill({3, Side::Sell, 48.0, 1.0, 3});

    REQUIRE(pnl.turnover() == Catch::Approx(50 * 2 + 49 * 1 + 48 * 1));
    REQUIRE(pnl.inventory() == Catch::Approx(0.0));
}
