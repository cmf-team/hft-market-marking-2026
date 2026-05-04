#include "analytics.hpp"

#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>

AnalyticsResult compute_analytics(const BacktestResult& res)
{
    AnalyticsResult result{};

    // Turnover: total notional traded
    for (const auto& f : res.fills)
        result.turnover += static_cast<double>(f.qty) * static_cast<double>(f.price) / PRICE_SCALE_F;

    // Max Drawdown is tracked per-tick inside run_backtest — just read it.
    result.max_drawdown = res.max_drawdown;

    const auto& series = res.pnl_series;
    if (series.size() > 1)
    {
        // Daily PnL bucketing and Sharpe ratio
        std::vector<double> daily_pnl;
        if (!res.pnl_timestamps.empty())
        {
            uint64_t day_start = res.pnl_timestamps.front() / US_PER_DAY;
            double day_open = series.front();
            for (size_t i = 1; i < series.size(); ++i)
            {
                const uint64_t day = res.pnl_timestamps[i] / US_PER_DAY;
                if (day != day_start)
                {
                    daily_pnl.push_back(series[i - 1] - day_open);
                    day_open = series[i - 1];
                    day_start = day;
                }
            }
            daily_pnl.push_back(series.back() - day_open);
        }

        if (daily_pnl.size() > 1)
        {
            const double mean_r = std::accumulate(daily_pnl.begin(), daily_pnl.end(), 0.0) / static_cast<double>(daily_pnl.size());
            double var = 0.0;
            for (double r : daily_pnl)
                var += (r - mean_r) * (r - mean_r);
            var /= static_cast<double>(daily_pnl.size() - 1);
            const double std_r = std::sqrt(var);
            if (std_r > 0.0)
                result.sharpe = mean_r / std_r * std::sqrt(252.0);
        }
        else
        {
            std::cerr << "Warning: fewer than 2 daily buckets in data — Sharpe ratio not computed.\n";
        }
    }

    // Win Rate — true position-walk round trips.
    //   A cycle opens when position leaves zero and closes when position
    //   returns to zero or crosses through it. Cycle PnL is the delta of
    //   running_realized between open-fill and close-fill.
    if (!res.fills.empty())
    {
        int64_t position = 0;
        double cycle_open_pnl = 0.0;
        uint64_t wins = 0, round_trips = 0;

        for (const auto& f : res.fills)
        {
            const int64_t signed_qty = (f.side == Side::Buy) ? f.qty : -f.qty;
            const int64_t old_pos = position;
            position += signed_qty;

            if (old_pos == 0 && position != 0)
            {
                cycle_open_pnl = f.running_realized; // open
            }
            else if (old_pos != 0 && position == 0)
            {
                if (f.running_realized > cycle_open_pnl)
                    ++wins; // flatten
                ++round_trips;
            }
            else if ((old_pos > 0) != (position > 0))
            {
                if (f.running_realized > cycle_open_pnl)
                    ++wins; // cross-through
                ++round_trips;
                cycle_open_pnl = f.running_realized;
            }
        }
        result.win_rate = (round_trips > 0)
                              ? static_cast<double>(wins) / static_cast<double>(round_trips)
                              : 0.0;
    }

    return result;
}
