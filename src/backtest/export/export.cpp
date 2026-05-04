#include "export.hpp"

#include <format>
#include <fstream>

void export_fills_csv(const BacktestResult& res, const std::string& path)
{
    std::ofstream csv(path);
    csv << "timestamp,order_id,side,price,qty,running_realized_pnl\n";
    for (const auto& f : res.fills)
    {
        csv << std::format("{},{},{},{},{},{:.8f}\n",
                           f.ts, f.order_id,
                           (f.side == Side::Buy ? "buy" : "sell"),
                           f.price, f.qty,
                           f.running_realized);
    }
}

void export_report_csv(const std::vector<std::tuple<std::string_view, BacktestResult, AnalyticsResult>>& results, const std::string& path)
{
    std::ofstream csv(path);
    csv << "algo,total_orders,fills,position,realized_pnl,unrealized_pnl,total_pnl,turnover,max_drawdown,sharpe,win_rate\n";

    for (const auto& report : results)
    {
        const auto& [algo, res, metrics] = report;

        csv << std::format(
            "{},{},{},{},{},{},{},{},{},{},{}\n",
            algo, res.pnl.total_orders, res.fills.size(), res.pnl.position,
            res.pnl.realized_pnl, res.pnl.unrealized_pnl, res.pnl.total_pnl(),
            metrics.turnover, metrics.max_drawdown, metrics.sharpe, metrics.win_rate * 100.0);
    }
}
