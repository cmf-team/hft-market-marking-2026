#include "backtest/analytics/analytics.hpp"
#include "backtest/engine/args.hpp"
#include "backtest/engine/engine.hpp"
#include "backtest/export/export.hpp"
#include "backtest/strategies/factory.hpp"

#include <chrono>
#include <format>
#include <iostream>
#include <span>

static std::tuple<std::string_view, BacktestResult, AnalyticsResult> run_one(size_t idx, const Config& cfg)
{
    MmapFile lob_file(cfg.lob_path);
    MmapFile trades_file(cfg.trades_path);
    LobReader lob_rdr(lob_file);
    TradeReader trade_rdr(trades_file);
    StrategyVariant strategy = make_strategy(cfg.strategy_types[idx], trade_rdr, cfg.target_qty);

    const auto sname = std::visit([](const auto& s)
                                  { return s.name(); }, strategy);
    std::cout << std::format("\nRunning backtest [{}]...\n", sname);

    const auto t0 = std::chrono::steady_clock::now();
    auto res = std::visit([&](auto& s)
                          { return run_backtest(lob_rdr, s, cfg); }, strategy);
    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(t1 - t0).count();
    const std::string csv_path = std::format("fills_{}_{}.csv", idx, sname);
    export_fills_csv(res, csv_path);
    const auto metrics = compute_analytics(res);

    const uint64_t trade_rows = std::visit([](const auto& s)
                                           { return s.trade_rows(); }, strategy);
    const auto& pnl = res.pnl;
    std::cout << std::format(
        "\n=== Backtest Results [{}] ===\n"
        "LOB rows parsed   : {}\n"
        "Trade rows parsed : {}\n"
        "Orders submitted  : {}\n"
        "Fills             : {}\n"
        "Final position    : {}\n"
        "Realized PnL      : {:.8f}\n"
        "Unrealized PnL    : {:.8f}\n"
        "Total PnL         : {:.8f}\n"
        "Max Drawdown      : {:.8f}\n"
        "Sharpe (daily~)   : {:.4f}\n"
        "Win Rate          : {:.2f}%\n"
        "Turnover          : {:.2f}\n"
        "Elapsed           : {:.3f} s\n"
        "Throughput        : {:.1f} M events/s\n"
        "Fills CSV         : {}\n"
        "Maker fee (bps)   : {:.2f}\n",
        sname,
        res.lob_rows, trade_rows,
        pnl.total_orders, res.fills.size(), pnl.position,
        pnl.realized_pnl, pnl.unrealized_pnl, pnl.total_pnl(),
        metrics.max_drawdown, metrics.sharpe, metrics.win_rate * 100.0,
        metrics.turnover, elapsed, (res.lob_rows + trade_rows) / elapsed / 1e6,
        csv_path, cfg.maker_fee_bps);

    return {sname, res, metrics};
}

int main(int argc, char* argv[])
{
    const Config cfg = parse_args(std::span(argv, argc));

    std::cout << std::format("Loading {} ...\n", cfg.lob_path);
    std::cout << std::format("Loading {} ...\n", cfg.trades_path);

    // Validate LOB file format once
    {
        MmapFile lob_file(cfg.lob_path);
        if (!validate_lob_file(lob_file))
        {
            std::cerr << "Error: LOB file failed validation (bad data or format)\n";
            return 1;
        }
    }

    std::vector<std::tuple<std::string_view, BacktestResult, AnalyticsResult>> results;
    for (size_t i = 0; i < cfg.strategy_types.size(); ++i)
    {
        std::tuple<std::string_view, BacktestResult, AnalyticsResult> res = run_one(i, cfg);
        results.push_back(res);
    }
    export_report_csv(results, "out.csv");

    return 0;
}
