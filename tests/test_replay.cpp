#include "backtest/replay_engine.hpp"
#include "backtest/test_strategy.hpp"

#include <catch2/catch_test_macros.hpp>

namespace backtest::test {

TEST_CASE("ReplayEngine: processes all events", "[replay]") {
    std::vector<MarketEvent> events;
    for (int i = 0; i < 1000; ++i) {
        events.push_back({
            1000 + i * 1000,
            1'000'000 + i,
            100,
            EventType::Trade,
            (i % 2 == 0) ? Side::Buy : Side::Sell,
        });
    }

    TestStrategy strategy;
    ReplayEngine engine(events, strategy, 0.0);
    engine.run();

    REQUIRE(engine.processedCount() == 1000);
    REQUIRE(engine.totalEvents() == 1000);
}

TEST_CASE("ReplayEngine: max-speed mode is fast", "[replay][performance]") {
    std::vector<MarketEvent> events;
    events.reserve(100'000);
    for (int i = 0; i < 100'000; ++i) {
        events.push_back({
            1000 + i * 1000,
            1'000'000,
            100,
            EventType::Trade,
            Side::Buy,
        });
    }

    TestStrategy strategy;
    strategy.enable_trade_logging = false;

    ReplayEngine engine(events, strategy, 0.0);

    auto start = std::chrono::high_resolution_clock::now();
    engine.run();
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    REQUIRE(elapsed.count() < 100);
}

TEST_CASE("ReplayEngine: time-controlled mode takes expected time", "[replay]") {
    std::vector<MarketEvent> events;
    for (int i = 0; i < 10; ++i) {
        events.push_back({
            1000 + i * 1000,
            1'000'000,
            100,
            EventType::Trade,
            Side::Buy,
        });
    }

    TestStrategy strategy;
    ReplayEngine engine(events, strategy, 1.0);

    auto start = std::chrono::high_resolution_clock::now();
    engine.run();
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    REQUIRE(elapsed.count() >= 8);
    REQUIRE(elapsed.count() <= 20);
}

}