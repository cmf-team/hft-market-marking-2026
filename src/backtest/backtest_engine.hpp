#pragma once

#include "backtest/backtest_config.hpp"
#include "backtest/backtest_data_reader.hpp"
#include "backtest/execution_simulator.hpp"
#include "strategy/strategy_interface.hpp"
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace hft_backtest {

struct BacktestSummary {
    std::string strategy_name;

    // PnL.
    double pnl_total      = 0.0;
    double pnl_realized   = 0.0;  // = cash - initial_cash - fees
    double pnl_unrealized = 0.0;  // = inventory * mark_price
    double max_drawdown   = 0.0;  // в "долларах" PnL: peak - trough
    double sharpe_per_step = 0.0; // безразмерный, по step-PnL

    // Производные метрики (заполняются в finalize()):
    //   fill_rate     = orders_filled / orders_placed   -- "съедают ли наши квоты"
    //   avg_trade_size = total_volume / orders_filled   -- средний fill в штуках
    //   calmar_ratio  = total_pnl / max_drawdown        -- profit на единицу боли
    double fill_rate      = 0.0;
    double avg_trade_size = 0.0;
    double calmar_ratio   = 0.0;

    // Trading.
    uint64_t orders_placed    = 0;
    uint64_t orders_cancelled = 0;
    uint64_t orders_filled    = 0;
    double   total_volume     = 0.0;  // в штуках
    double   total_turnover   = 0.0;  // в "долларах": sum(price * qty)
    double   total_fees       = 0.0;

    // Inventory.
    double final_inventory = 0.0;
    double max_long_inv    = 0.0;
    double max_short_inv   = 0.0;

    // Time.
    uint64_t start_ts_us = 0;
    uint64_t end_ts_us   = 0;
    uint64_t events_processed = 0;
};

class BacktestEngine {
public:
    explicit BacktestEngine(const BacktestConfig& cfg);

    bool load_data();   // использует пути из cfg_
    void run();

    const BacktestSummary& summary() const { return summary_; }

    void print_summary(std::ostream& os) const;
    void export_summary(const std::string& path) const;

private:
    void build_strategy();
    void on_snapshot(const OrderBookSnapshot& snap, uint64_t ts);
    void apply_fills(const std::vector<FillReport>& fills, double mark_px);
    void log_timeseries_row(const OrderBookSnapshot& snap, uint64_t ts);
    // Считает derived-метрики (sharpe, fill_rate, avg_trade_size, calmar_ratio,
    // финальные счётчики ордеров и fees). Вызывается один раз в конце run().
    void finalize_summary();

    BacktestConfig cfg_;
    BacktestDataReader reader_;
    std::unique_ptr<IStrategy> strategy_;
    std::unique_ptr<ExecutionSimulator> exec_;
    BacktestSummary summary_;

    // PnL tracking.
    double prev_total_pnl_ = 0.0;
    double peak_total_pnl_ = 0.0;
    std::vector<double> step_pnls_;  // для Sharpe-подобной метрики

    std::ofstream timeseries_;
    uint64_t      events_seen_ = 0;
};

}  // namespace hft_backtest
