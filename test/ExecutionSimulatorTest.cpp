// Тесты модели исполнения: проверяем walk-the-book, partial fill,
// queue_priority, cancel-all.

#include "backtest/execution_simulator.hpp"
#include "test_helpers.hpp"

#include "catch2/catch_all.hpp"

using namespace hft_backtest;
using hft_backtest::test::make_snap;

TEST_CASE("ExecutionSimulator: BUY does not fill when ask above limit",
          "[execution]")
{
    ExecutionSimulator exec({});
    StrategyAction a;
    a.quotes.push_back({Side::BUY, /*price=*/1000, /*qty=*/5});
    auto placed = exec.apply_action(a, /*now=*/100);
    REQUIRE(placed.size() == 1);
    REQUIRE(exec.orders_placed() == 1);

    auto snap = make_snap(/*ts=*/200, {{990, 10}}, {{1010, 10}});
    auto fills = exec.match_against_book(snap, 200);
    REQUIRE(fills.empty());
    REQUIRE(exec.orders_filled() == 0);
}

TEST_CASE("ExecutionSimulator: BUY fills at best_ask when ask <= limit",
          "[execution]")
{
    ExecutionSimulator exec({});
    StrategyAction a;
    a.quotes.push_back({Side::BUY, 1010, 5});
    exec.apply_action(a, 100);

    auto snap = make_snap(200, {{990, 10}}, {{1005, 10}});
    auto fills = exec.match_against_book(snap, 200);
    REQUIRE(fills.size() == 1);
    REQUIRE(fills[0].side == Side::BUY);
    REQUIRE(fills[0].price == 1005);
    REQUIRE(fills[0].quantity == 5);
    REQUIRE(exec.orders_filled() == 1);
    REQUIRE(exec.active_orders().empty());
}

TEST_CASE("ExecutionSimulator: BUY walks several ask levels for big orders",
          "[execution]")
{
    ExecutionSimulator exec({});
    StrategyAction a;
    a.quotes.push_back({Side::BUY, 1020, 12});
    exec.apply_action(a, 100);

    auto snap = make_snap(200, {{990, 100}},
                          {{1000, 3}, {1010, 4}, {1020, 5}, {1030, 100}});
    auto fills = exec.match_against_book(snap, 200);
    REQUIRE(fills.size() == 3);
    REQUIRE(fills[0].quantity == 3);
    REQUIRE(fills[0].price    == 1000);
    REQUIRE(fills[1].quantity == 4);
    REQUIRE(fills[1].price    == 1010);
    REQUIRE(fills[2].quantity == 5);
    REQUIRE(fills[2].price    == 1020);
    REQUIRE(exec.active_orders().empty());
}

TEST_CASE("ExecutionSimulator: partial fill keeps remainder resting",
          "[execution]")
{
    ExecutionSimulator exec({});
    StrategyAction a;
    a.quotes.push_back({Side::SELL, 1000, 10});
    exec.apply_action(a, 100);

    auto snap = make_snap(200, {{1005, 4}}, {{1010, 100}});
    auto fills = exec.match_against_book(snap, 200);
    REQUIRE(fills.size() == 1);
    REQUIRE(fills[0].quantity == 4);
    REQUIRE(exec.active_orders().size() == 1);
    REQUIRE(exec.active_orders().front().remaining_qty == 6);
}

TEST_CASE("ExecutionSimulator: queue_priority skips fill on placement tick",
          "[execution]")
{
    ExecutionConfig cfg;
    cfg.queue_priority = true;
    ExecutionSimulator exec(cfg);
    StrategyAction a;
    a.quotes.push_back({Side::BUY, 1010, 5});
    exec.apply_action(a, /*now=*/200);

    auto snap = make_snap(200, {{990, 10}}, {{1000, 10}});
    auto fills = exec.match_against_book(snap, 200);
    REQUIRE(fills.empty());
    REQUIRE(exec.active_orders().size() == 1);

    auto snap2 = make_snap(300, {{990, 10}}, {{1000, 10}});
    fills = exec.match_against_book(snap2, 300);
    REQUIRE(fills.size() == 1);
}

TEST_CASE("ExecutionSimulator: cancel_all wipes the resting book",
          "[execution]")
{
    ExecutionSimulator exec({});
    StrategyAction a;
    a.quotes.push_back({Side::BUY,  999,  5});
    a.quotes.push_back({Side::SELL, 1011, 5});
    exec.apply_action(a, 100);
    REQUIRE(exec.active_orders().size() == 2);

    StrategyAction b;
    b.cancels.push_back({/*cancel_all=*/true, 0});
    exec.apply_action(b, 200);
    REQUIRE(exec.active_orders().empty());
    REQUIRE(exec.orders_cancelled() == 2);
}
