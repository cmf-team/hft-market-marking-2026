// Backtest entry point. Usage:
//
//   hft-market-making [config.cfg] [--lob path] [--trades path] \
//                     [--strategy naive|as2008|as2018]
//
// Если config.cfg указан -- грузим его. CLI-флаги переопределяют значения.

#include "backtest/backtest_config.hpp"
#include "backtest/backtest_engine.hpp"

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>

using namespace hft_backtest;

namespace
{

void print_usage(const char* argv0)
{
    std::cerr << "Usage: " << argv0 << " [config.cfg] [options]\n"
              << "  --lob <path>             override lob_path\n"
              << "  --trades <path>          override trades_path\n"
              << "  --strategy <kind>        naive | as2008 | as2018\n"
              << "  --max-events <N>         stop after N snapshots\n"
              << "  --no-trades              skip parsing trades.csv (~5x speedup for AS)\n"
              << "  --gamma <x>              AS gamma\n"
              << "  --k <x>                  AS k\n"
              << "  --order-size <x>         per-side quote size\n"
              << "  --max-inventory <x>      hard cap on |inventory|\n"
              << "  --sigma-window <N>       rolling window for sigma\n"
              << "  --T-seconds <x>          AS time horizon\n"
              << "  --summary <path>         where to write summary CSV\n"
              << "  --timeseries <path>      where to write per-step CSV\n";
}

bool need_arg(int& i, int argc, const char* /*argv*/[], const char* flag)
{
    if (i + 1 >= argc)
    {
        std::cerr << "Missing value for " << flag << "\n";
        return false;
    }
    ++i;
    return true;
}

} // namespace

int main(int argc, const char* argv[])
{
    BacktestConfig cfg;

    int i = 1;
    if (i < argc && argv[i][0] != '-')
    {
        if (!ConfigLoader::load(argv[i], cfg)) return 2;
        ++i;
    }

    for (; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") { print_usage(argv[0]); return 0; }
        else if (a == "--lob"     && need_arg(i, argc, argv, "--lob"))     cfg.lob_path = argv[i];
        else if (a == "--trades"  && need_arg(i, argc, argv, "--trades"))  cfg.trades_path = argv[i];
        else if (a == "--strategy" && need_arg(i, argc, argv, "--strategy"))
        {
            std::string v = argv[i];
            if      (v == "naive")  cfg.strategy = StrategyKind::Naive;
            else if (v == "as2008") cfg.strategy = StrategyKind::AvellanedaStoikov2008;
            else if (v == "as2018") cfg.strategy = StrategyKind::AvellanedaStoikov2018Micro;
            else { std::cerr << "Unknown strategy: " << v << "\n"; return 2; }
        }
        else if (a == "--max-events"    && need_arg(i, argc, argv, "--max-events"))    cfg.max_events = std::stoull(argv[i]);
        else if (a == "--no-trades")    cfg.load_trades = false;
        else if (a == "--gamma"         && need_arg(i, argc, argv, "--gamma"))         cfg.as_cfg.gamma = std::stod(argv[i]);
        else if (a == "--k"             && need_arg(i, argc, argv, "--k"))             cfg.as_cfg.k = std::stod(argv[i]);
        else if (a == "--order-size"    && need_arg(i, argc, argv, "--order-size"))    cfg.as_cfg.order_size = std::stod(argv[i]);
        else if (a == "--max-inventory" && need_arg(i, argc, argv, "--max-inventory")) cfg.as_cfg.max_inventory = std::stod(argv[i]);
        else if (a == "--sigma-window"  && need_arg(i, argc, argv, "--sigma-window"))  cfg.as_cfg.sigma_window = std::stoull(argv[i]);
        else if (a == "--T-seconds"     && need_arg(i, argc, argv, "--T-seconds"))     cfg.as_cfg.T_seconds = std::stod(argv[i]);
        else if (a == "--summary"       && need_arg(i, argc, argv, "--summary"))       cfg.summary_csv = argv[i];
        else if (a == "--timeseries"    && need_arg(i, argc, argv, "--timeseries"))    cfg.timeseries_csv = argv[i];
        else { std::cerr << "Unknown option: " << a << "\n"; print_usage(argv[0]); return 2; }
    }

    std::cerr << "[backtest] strategy="
              << (cfg.strategy == StrategyKind::Naive ? "naive"
                : cfg.strategy == StrategyKind::AvellanedaStoikov2008 ? "as2008"
                : "as2018")
              << " lob=" << cfg.lob_path
              << " trades=" << cfg.trades_path << "\n";

    try
    {
        BacktestEngine engine(cfg);
        if (!engine.load_data())
        {
            std::cerr << "Failed to load market data.\n";
            return 1;
        }

        const auto t0 = std::chrono::steady_clock::now();
        engine.run();
        const auto t1 = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        std::cerr << "[backtest] finished in " << ms << " ms\n";

        engine.print_summary(std::cout);
        if (!cfg.summary_csv.empty())
        {
            engine.export_summary(cfg.summary_csv);
            std::cout << "\nSummary written to:    " << cfg.summary_csv << "\n";
        }
        if (!cfg.timeseries_csv.empty())
        {
            std::cout << "Timeseries written to: " << cfg.timeseries_csv << "\n";
        }
    }
    catch (const std::exception& ex)
    {
        std::cerr << "HFT market-making backtester threw: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
