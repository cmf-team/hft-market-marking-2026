#include "backtest/execution_engine.hpp"
#include "backtest/market_event.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>


namespace backtest::test {

TEST_CASE("ExecutionEngine: Limit Buy Order executes on price cross", "[execution]") {
    ExecutionEngine engine(10);

    auto order = Order::limit_buy(
        1'000'000,  // price_ticks
        100,        // quantity
        1,          // order_id
        1000        // timestamp_us
    );

    SECTION("Price above limit → rejected") {
        MarketEvent event{2000, 1'000'100, 50, EventType::Trade,  Side::Sell};
        auto report = engine.checkLimitOrder(order, event);

        REQUIRE(report.status == OrderStatus::Rejected);
        REQUIRE(report.filled_qty == 0);
    }

    SECTION("Price equals limit → filled") {
        MarketEvent event{2000, 1'000'000, 50, EventType::Trade, Side::Sell};
        auto report = engine.checkLimitOrder(order, event);

        REQUIRE(report.status == OrderStatus::Filled);
        REQUIRE(report.filled_qty == 100);
        REQUIRE(report.avg_price == 1'000'000);
    }

    SECTION("Price below limit → filled (better price)") {
        MarketEvent event{2000, 999'900, 50, EventType::Trade, Side::Sell};
        auto report = engine.checkLimitOrder(order, event);

        REQUIRE(report.status == OrderStatus::Filled);
        REQUIRE(report.filled_qty == 100);
        REQUIRE(report.avg_price == 999'900);
    }
}

TEST_CASE("ExecutionEngine: Limit Sell Order executes on price cross", "[execution]") {
    ExecutionEngine engine(10);

    auto order = Order::limit_sell(
        1'000'000,
        100,
        1,
        1000
    );

    SECTION("Price below limit → rejected") {
        MarketEvent event{2000, 999'900, 50, EventType::Trade, Side::Buy};
        auto report = engine.checkLimitOrder(order, event);
        REQUIRE(report.status == OrderStatus::Rejected);
    }

    SECTION("Price equals limit → filled") {
        MarketEvent event{2000, 1'000'000, 50, EventType::Trade, Side::Buy};
        auto report = engine.checkLimitOrder(order, event);
        REQUIRE(report.status == OrderStatus::Filled);
    }

    SECTION("Price above limit → filled (better price)") {
        MarketEvent event{2000, 1'000'100, 50, EventType::Trade, Side::Buy};
        auto report = engine.checkLimitOrder(order, event);
        REQUIRE(report.status == OrderStatus::Filled);
        REQUIRE(report.avg_price == 1'000'100);
    }
}

TEST_CASE("ExecutionEngine: Market Order executes immediately", "[execution]") {
    ExecutionEngine engine(10);

    auto order = Order::market_buy(100, 1, 1000);
    MarketEvent event{2000, 1'000'000, 50, EventType::Trade, Side::Sell};

    auto report = engine.executeMarketOrder(order, event);

    REQUIRE(report.status == OrderStatus::Filled);
    REQUIRE(report.filled_qty == 100);
    REQUIRE(report.avg_price == 1'000'000);

    REQUIRE(report.commission == 10'000);
}

TEST_CASE("ExecutionEngine: Inactive order is rejected", "[execution]") {
    ExecutionEngine engine(10);

    auto order = Order::limit_buy(1'000'000, 100, 1, 1000);
    order.status = OrderStatus::Filled;

    MarketEvent event{2000, 1'000'000, 50, EventType::Trade, Side::Sell};
    auto report = engine.checkLimitOrder(order, event);

    REQUIRE(report.status == OrderStatus::Rejected);
}

TEST_CASE("ExecutionEngine: Commission calculation is correct", "[execution]") {
    ExecutionEngine engine(10);

    auto order = Order::market_buy(100, 1, 1000);
    MarketEvent event{2000, 1'000'000, 50, EventType::Trade, Side::Sell};

    auto report = engine.executeMarketOrder(order, event);

    REQUIRE(report.commission == 10'000);

    double commission_usd = static_cast<double>(report.commission) / 10'000'000.0;
    REQUIRE_THAT(commission_usd, Catch::Matchers::WithinAbs(0.001, 0.0000001));
}

}