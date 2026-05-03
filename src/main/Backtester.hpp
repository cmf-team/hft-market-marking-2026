#pragma once

#include "common/Types.hpp"
#include "main/AvellanedaStoikov.hpp"

#include <climits>
#include <cstdint>
#include <string>

namespace cmf
{

struct BacktestArgs
{
    std::string trades_path = "MD/trades.csv";
    std::string lob_path = "MD/lob.csv";
    std::string output_path = "equity_curve.csv";
    std::string strategy = "none";
    MicroTime start_time = 0;
    MicroTime end_time = INT64_MAX;
    MicroTime mtm_interval_us = 1'000'000;
    bool partial_fills = true;
    std::uint64_t max_events = 0;

    AvellanedaStoikovConfig as;
};

BacktestArgs parseArgs(int argc, const char* argv[]);

int runBacktest(const BacktestArgs& args);

} // namespace cmf
