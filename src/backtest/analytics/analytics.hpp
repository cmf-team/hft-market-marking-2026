#pragma once

#include "backtest/engine/engine.hpp"

#include <cmath>
#include <iostream>
#include <numeric>
#include <ranges>
#include <vector>

struct AnalyticsResult
{
    double max_drawdown;
    double sharpe;
    double win_rate;
    double turnover;
};

AnalyticsResult compute_analytics(const BacktestResult& res);
