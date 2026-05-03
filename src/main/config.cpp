#include "config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace hft {

namespace {


std::string trim_copy(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), not_space));
    value.erase(
        std::find_if(value.rbegin(), value.rend(), not_space).base(),
        value.end());
    return value;
}


bool parse_bool_or_throw(const std::string& key, const std::string& value) {
    if (value == "1" || value == "true" || value == "TRUE" || value == "True") {
        return true;
    }
    if (value == "0" || value == "false" || value == "FALSE" ||
        value == "False") {
        return false;
    }
    throw std::runtime_error("Invalid boolean value for key '" + key +
                             "': " + value);
}

}

BacktestConfig load_config(const std::string& config_path) {
    std::ifstream input(config_path);
    if (!input.is_open()) {
        throw std::runtime_error("Cannot open config file: " + config_path);
    }

    BacktestConfig cfg;
    std::string line;
    std::size_t line_no = 0;

    while (std::getline(input, line)) {
        ++line_no;
        std::string stripped = trim_copy(line);

        if (stripped.empty() || stripped[0] == '#' || stripped[0] == ';') {
            continue;
        }

        const std::size_t pos = stripped.find('=');
        if (pos == std::string::npos) {
            continue;
        }

        const std::string key = trim_copy(stripped.substr(0, pos));
        const std::string value = trim_copy(stripped.substr(pos + 1));
        if (key.empty()) {
            continue;
        }

        try {


            if (key == "strategy") {
                cfg.strategy = value;
            } else if (key == "lob_path") {
                cfg.lob_path = value;
            } else if (key == "trades_path") {
                cfg.trades_path = value;
            } else if (key == "report_path") {
                cfg.report_path = value;
            } else if (key == "max_total_events") {
                cfg.max_total_events = static_cast<std::size_t>(std::stoull(value));
            } else if (key == "max_lob_events") {
                cfg.max_lob_events = static_cast<std::size_t>(std::stoull(value));
            } else if (key == "max_trade_events") {
                cfg.max_trade_events =
                    static_cast<std::size_t>(std::stoull(value));
            } else if (key == "replay_speed") {
                cfg.replay_speed = std::stod(value);
            } else if (key == "initial_cash") {
                cfg.initial_cash = std::stod(value);
            } else if (key == "order_qty") {
                cfg.order_qty = std::stod(value);
            } else if (key == "take_profit_bps") {
                cfg.take_profit_bps = std::stod(value);
            } else if (key == "entry_refresh_us") {
                cfg.entry_refresh_us = std::stoll(value);
            } else if (key == "max_position") {
                cfg.max_position = std::stoll(value);
            } else if (key == "include_trade_events") {
                cfg.include_trade_events = parse_bool_or_throw(key, value);
            } else if (key == "fill_on_touch") {
                cfg.fill_on_touch = parse_bool_or_throw(key, value);
            } else if (key == "as_gamma") {
                cfg.as_gamma = std::stod(value);
            } else if (key == "as_k") {
                cfg.as_k = std::stod(value);
            } else if (key == "as_sigma") {
                cfg.as_sigma = std::stod(value);
            } else if (key == "as_sigma_floor") {
                cfg.as_sigma_floor = std::stod(value);
            } else if (key == "as_volatility_ewma_alpha") {
                cfg.as_volatility_ewma_alpha = std::stod(value);
            } else if (key == "as_horizon_us") {
                cfg.as_horizon_us = std::stoll(value);
            } else if (key == "as_quote_refresh_us") {
                cfg.as_quote_refresh_us = std::stoll(value);
            } else if (key == "as_tick_size") {
                cfg.as_tick_size = std::stod(value);
            } else if (key == "as_min_spread_ticks") {
                cfg.as_min_spread_ticks = std::stod(value);
            } else if (key == "as_spread_multiplier") {
                cfg.as_spread_multiplier = std::stod(value);
            } else if (key == "as_use_microprice") {
                cfg.as_use_microprice = parse_bool_or_throw(key, value);
            } else if (key == "as_microprice_alpha") {
                cfg.as_microprice_alpha = std::stod(value);
            }
        } catch (const std::exception& ex) {
            throw std::runtime_error("Config parse error in " + config_path +
                                     " line " + std::to_string(line_no) + ": " +
                                     ex.what());
        }
    }

    if (cfg.lob_path.empty()) {
        throw std::runtime_error("Config value 'lob_path' is required");
    }
    if (cfg.include_trade_events && cfg.trades_path.empty()) {
        throw std::runtime_error(
            "Config value 'trades_path' is required when include_trade_events=true");
    }

    return cfg;
}

}
