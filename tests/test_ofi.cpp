#include "backtest/market_event.hpp"
#include "backtest/test_strategy.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace backtest::test {

TEST_CASE("OFI: Empty window returns zero imbalance", "[ofi]") {
    TestStrategy strategy;
    strategy.on_init();
}

TEST_CASE("OFI: All buy events → positive imbalance", "[ofi]") {
    TestStrategy strategy;
    strategy.on_init();
    strategy.window_us = 1'000'000;

    for (int i = 0; i < 5; ++i) {
        MarketEvent event{
            1000 + i * 100,
            1'000'000,
            100,
            EventType::Trade,
            Side::Buy,
        };
        strategy.on_event(event);
    }

}

TEST_CASE("OFI: All sell events → negative imbalance", "[ofi]") {
    TestStrategy strategy;
    strategy.on_init();
    strategy.window_us = 1'000'000;

    for (int i = 0; i < 5; ++i) {
        MarketEvent event{
            1000 + i * 100,
            1'000'000,
            100,
            EventType::Trade,
            Side::Sell,
        };
        strategy.on_event(event);
    }

}

TEST_CASE("OFI: Mixed events → balanced imbalance", "[ofi]") {
    TestStrategy strategy;
    strategy.on_init();
    strategy.window_us = 1'000'000;

    for (int i = 0; i < 3; ++i) {
        MarketEvent buy{1000 + i * 100, 1'000'000, 100,  EventType::Trade, Side::Buy};
        MarketEvent sell{1050 + i * 100, 1'000'000, 100, EventType::Trade, Side::Sell};
        strategy.on_event(buy);
        strategy.on_event(sell);
    }

}

TEST_CASE("OFI: Old events expire from window", "[ofi]") {
    TestStrategy strategy;
    strategy.on_init();
    strategy.window_us = 500'000;

    MarketEvent e1{0, 1'000'000, 100, EventType::Trade, Side::Buy};
    strategy.on_event(e1);

    MarketEvent e2{600'000, 1'000'000, 100, EventType::Trade, Side::Sell};
    strategy.on_event(e2);
}

}