#include "backtest/backtest_config.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace hft_backtest {

namespace {

std::string trim(const std::string& s) {
    auto a = s.find_first_not_of(" \t\r\n");
    auto b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, b - a + 1);
}

bool to_bool(const std::string& v) {
    auto lower = v;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return lower == "1" || lower == "true" || lower == "yes" || lower == "on";
}

bool apply(const std::string& k, const std::string& v, BacktestConfig& cfg) {
    try {
        if      (k == "lob_path")             cfg.lob_path = v;
        else if (k == "trades_path")          cfg.trades_path = v;
        else if (k == "load_trades")          cfg.load_trades = to_bool(v);
        else if (k == "strategy") {
            if      (v == "naive")     cfg.strategy = StrategyKind::Naive;
            else if (v == "as2008")    cfg.strategy = StrategyKind::AvellanedaStoikov2008;
            else if (v == "as2018")    cfg.strategy = StrategyKind::AvellanedaStoikov2018Micro;
            else if (v == "as_micro")  cfg.strategy = StrategyKind::AvellanedaStoikov2018Micro;
            else { std::cerr << "Unknown strategy: " << v << "\n"; return false; }
        }
        else if (k == "initial_cash")         cfg.initial_cash         = std::stod(v);
        else if (k == "start_time_us")        cfg.start_time_us        = std::stoull(v);
        else if (k == "end_time_us")          cfg.end_time_us          = std::stoull(v);
        else if (k == "max_events")           cfg.max_events           = std::stoull(v);
        else if (k == "progress_interval")    cfg.progress_interval    = std::stoull(v);
        else if (k == "print_progress")       cfg.print_progress       = to_bool(v);
        else if (k == "transaction_cost_bps") cfg.transaction_cost_bps = std::stod(v);
        else if (k == "slippage_bps")         cfg.slippage_bps         = std::stod(v);
        else if (k == "queue_priority")       cfg.queue_priority       = to_bool(v);
        else if (k == "gamma")                cfg.as_cfg.gamma         = std::stod(v);
        else if (k == "k")                    cfg.as_cfg.k             = std::stod(v);
        else if (k == "T_seconds")            cfg.as_cfg.T_seconds     = std::stod(v);
        else if (k == "sigma_window")         cfg.as_cfg.sigma_window  = std::stoull(v);
        else if (k == "order_size")           cfg.as_cfg.order_size    = std::stod(v);
        else if (k == "tick_size_cents")      cfg.as_cfg.tick_size_cents = std::stoull(v);
        else if (k == "use_microprice")       cfg.as_cfg.use_microprice = to_bool(v);
        else if (k == "enable_inventory_skew")cfg.as_cfg.enable_inventory_skew = to_bool(v);
        else if (k == "max_inventory")        cfg.as_cfg.max_inventory = std::stod(v);
        else if (k == "as_initial_cash")      cfg.as_cfg.initial_cash  = std::stod(v);
        else if (k == "summary_csv")          cfg.summary_csv          = v;
        else if (k == "timeseries_csv")       cfg.timeseries_csv       = v;
        else if (k == "timeseries_step")      cfg.timeseries_step      = std::stoull(v);
        else {
            std::cerr << "Warning: unknown config key '" << k << "'\n";
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "Failed to parse '" << k << " = " << v << "': " << ex.what() << "\n";
        return false;
    }
}

}  // namespace

bool ConfigLoader::load(const std::string& path, BacktestConfig& cfg) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "Cannot open config file: " << path << "\n";
        return false;
    }
    std::string line;
    while (std::getline(f, line)) {
        auto trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#' || trimmed[0] == '[') continue;
        auto eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        auto key = trim(trimmed.substr(0, eq));
        auto val = trim(trimmed.substr(eq + 1));
        if (auto hash = val.find_first_of("#;"); hash != std::string::npos) {
            val = trim(val.substr(0, hash));
        }
        apply(key, val, cfg);
    }
    return true;
}

}  // namespace hft_backtest
