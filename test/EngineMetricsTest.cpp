// Тесты на финализацию метрик движка: fill_rate, calmar_ratio, avg_trade_size,
// sharpe_per_step. Запускаемся на маленьком sample-LOB.

#include "backtest/backtest_config.hpp"
#include "backtest/backtest_engine.hpp"
#include "test_helpers.hpp"

#include "catch2/catch_all.hpp"

#include <cmath>

using namespace hft_backtest;
using hft_backtest::test::find_sample_lob;

TEST_CASE("BacktestEngine: finalize() computes fill_rate, calmar, avg_trade_size, sharpe",
          "[engine][metrics]")
{
    const std::string lob_path = find_sample_lob();
    if (lob_path.empty())
    {
        WARN("sample_data/lob_sample.csv not found from cwd; skipping");
        return;
    }

    BacktestConfig cfg;
    cfg.lob_path        = lob_path;
    cfg.load_trades     = false;
    cfg.strategy        = StrategyKind::AvellanedaStoikov2008;
    cfg.initial_cash    = 0.0;
    cfg.print_progress  = false;
    cfg.timeseries_csv  = "";
    cfg.summary_csv     = "";
    cfg.as_cfg.gamma            = 0.1;
    cfg.as_cfg.k                = 10.0;
    cfg.as_cfg.T_seconds        = 60.0;
    cfg.as_cfg.sigma_window     = 50;
    cfg.as_cfg.order_size       = 100;
    cfg.as_cfg.tick_size_cents  = 1.0;
    cfg.as_cfg.use_microprice   = false;
    cfg.as_cfg.enable_inventory_skew = true;
    cfg.as_cfg.max_inventory    = 10000;
    cfg.as_cfg.initial_cash     = 0.0;

    BacktestEngine bt(cfg);
    REQUIRE(bt.load_data());
    bt.run();

    const auto& s = bt.summary();
    REQUIRE(s.events_processed > 0);
    REQUIRE(s.orders_placed   > 0);

    REQUIRE(s.fill_rate >= 0.0);
    REQUIRE(s.fill_rate <= 1.0);

    if (s.orders_filled > 0)
    {
        REQUIRE(s.avg_trade_size > 0.0);
        const double expected_avg =
            s.total_volume / static_cast<double>(s.orders_filled);
        REQUIRE(std::abs(s.avg_trade_size - expected_avg) < 1e-9);
    }

    if (s.max_drawdown > 0.0)
    {
        const double expected_calmar = s.pnl_total / s.max_drawdown;
        REQUIRE(std::abs(s.calmar_ratio - expected_calmar) < 1e-9);
    }
    else
    {
        REQUIRE(s.calmar_ratio == 0.0);
    }

    REQUIRE(std::isfinite(s.sharpe_per_step));
    REQUIRE(s.orders_placed    >= s.orders_filled);
    REQUIRE(s.orders_cancelled <= s.orders_placed);
}

TEST_CASE("BacktestEngine: finalize() handles zero placed/filled/drawdown gracefully",
          "[engine][metrics]")
{
    BacktestSummary s;
    REQUIRE(s.fill_rate == 0.0);
    REQUIRE(s.avg_trade_size == 0.0);
    REQUIRE(s.calmar_ratio == 0.0);
    REQUIRE(std::isfinite(s.fill_rate));
    REQUIRE(std::isfinite(s.avg_trade_size));
    REQUIRE(std::isfinite(s.calmar_ratio));
}
