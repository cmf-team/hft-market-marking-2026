#pragma once

#include "backtest/engine/engine.hpp"
#include "backtest/analytics/analytics.hpp"
#include <format>
#include <fstream>

void export_fills_csv(const BacktestResult& res, const std::string& path = "fills.csv");
void export_report_csv(const std::vector<std::tuple<std::string_view, BacktestResult, AnalyticsResult>>& results, const std::string& path = "out.csv");