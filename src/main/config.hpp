#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace hft {


struct BacktestConfig {

    std::string strategy = "simple_cycle";

    std::string lob_path;
    std::string trades_path;
    std::string report_path = "reports/performance_report.md";


    std::size_t max_total_events = 0;
    std::size_t max_lob_events = 0;
    std::size_t max_trade_events = 0;


    double replay_speed = 0.0;
    double initial_cash = 0.0;


    double order_qty = 1'000.0;
    double take_profit_bps = 2.0;
    std::int64_t entry_refresh_us = 1'000'000;
    std::int64_t max_position = 1'000'000;


    bool include_trade_events = true;
    bool fill_on_touch = true;


    double as_gamma = 0.0001;
    double as_k = 20000000.0;
    double as_sigma = 0.0;
    double as_sigma_floor = 0.00000005;
    double as_volatility_ewma_alpha = 0.05;
    std::int64_t as_horizon_us = 60000000;
    std::int64_t as_quote_refresh_us = 500000;
    double as_tick_size = 0.0000001;
    double as_min_spread_ticks = 1.0;
    double as_spread_multiplier = 1.0;
    bool as_use_microprice = false;
    double as_microprice_alpha = 1.0;
};


BacktestConfig load_config(const std::string& config_path);

}
