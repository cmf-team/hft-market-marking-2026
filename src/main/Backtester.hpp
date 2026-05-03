#pragma once

#include "common/BasicTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cmf
{

struct BacktestConfig
{
    std::string lobCsvPath;
    std::string tradesCsvPath;
    std::string dir_path = R"(S:\YandexDisk\CMF\)";
    std::string outputCsvPath = dir_path+ "closed_trades.csv";

    std::string centerMode = "reservation"; // mid or reservation
    std::string fillMode = "overlap";       // overlap or trade_only

    double orderVolume = 1.0;
    double quoteHalfSpreadTicks = 1.0;
    double takeProfitTicks = 2.0;
    double stopLossTicks = 4.0;
    std::int64_t maxHoldMicros = 3'000'000;

    double riskAversion = 0.1;
    double volatilityPerSqrtSecond = 0.00001;
    double timeHorizonSeconds = 3.0;
    double tickSize = 0.0; // 0 means infer from LOB
};

struct BacktestSummary
{
    std::size_t lobRows = 0;
    std::size_t tradeRows = 0;
    std::size_t closedTrades = 0;
    double totalPnl = 0.0;
};

BacktestConfig parseBacktestArgs(int argc, const char* argv[]);
BacktestSummary runBacktest(const BacktestConfig& config);

} // namespace cmf
