#pragma once
#include <charconv>
#include <format>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "backtest/strategies/factory.hpp"

struct Config
{
    std::string lob_path;
    std::string trades_path;
    double maker_fee_bps = 0.0; // negative = rebate
    size_t pnl_sample_interval = 1'000;
    std::vector<StrategyType> strategy_types;
    int64_t target_qty = 100'000;
};

template <typename T>
void parse_numeric(std::string_view val, T& out, std::string_view flag)
{
    auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), out);
    if (ec != std::errc{})
    {
        std::cerr << std::format("Error: invalid value '{}' for {}\n", val, flag);
        std::exit(1);
    }
}

void print_usage(std::string_view prog);
Config parse_args(std::span<char*> args);
