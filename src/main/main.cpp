#include "engine/Engine.hpp"
#include "engine/LatencyModel.hpp"
#include "engine/Metrics.hpp"
#include "io/CsvReader.hpp"
#include "strategy/AvellanedaStoikov.hpp"
#include "strategy/Config.hpp"
#include "strategy/Intensity.hpp"

#include <fstream>
#include <iostream>
#include <string>

using namespace cmf;

static void write_csv(const std::string& path, const std::string& header,
                      const std::vector<std::string>& rows)
{
    std::ofstream f(path);
    f << header << "\n";
    for (const auto& r : rows)
        f << r << "\n";
}

int main(int argc, char** argv)
{
    KvConfig cfg = KvConfig::from_file(argc > 1 ? argv[1] : "");
    cfg.apply_cli_overrides(argc, argv);

    const std::string lobPath = cfg.get_string("data.lob", "lob.csv");
    const std::string trdPath = cfg.get_string("data.trades", "trades.csv");
    const double gamma = cfg.get_double("gamma", 0.1);
    const double volHalfLife = cfg.get_double("vol_halflife_ticks", 2000.0);
    const double invCap = cfg.get_double("inv_cap", 10000.0);
    const double baseQty = cfg.get_double("base_qty", 1000.0);
    const double sessionTicks = cfg.get_double("session_horizon_ticks", 1e6);
    const double latencyNs = cfg.get_double("latency_ns", 500000.0);
    const double feesMaker = cfg.get_double("fees.maker", -0.00005);
    const double feesTaker = cfg.get_double("fees.taker", 0.0007);
    const RefPriceMode mode = cfg.get_string("ref_price_mode", "mid") == "microprice"
                                  ? RefPriceMode::Microprice
                                  : RefPriceMode::Mid;

    double k;
    if (cfg.has("k"))
    {
        k = cfg.get_double("k", 1.5);
        std::cout << "k=" << k << " (from config)\n";
    }
    else
    {
        FitConfig fitCfg;
        fitCfg.buckets = cfg.get_int("intensity_fit_buckets", 20);
        fitCfg.maxDist = cfg.get_double("intensity_fit_max_dist", 5e-4);
        fitCfg.maxTrades = static_cast<std::size_t>(
            cfg.get_int("intensity_fit_max_trades", 2000000));
        const auto fit = fit_intensity_from_csvs(lobPath, trdPath, fitCfg);
        k = fit.valid ? fit.k : 1.5;
        std::cout << "intensity_fit: k=" << k << " A=" << fit.A
                  << " R2=" << fit.r2 << (fit.valid ? "" : " (fallback)") << "\n";
    }

    Logger::set_path("stoikov_mm.log");
    Fees fees{feesMaker, feesTaker};
    auto latency = std::make_unique<ConstLatency>(static_cast<NanoTime>(latencyNs));
    auto strat = std::make_unique<AvellanedaStoikovMM>(
        mode, gamma, k, sessionTicks, invCap, baseQty, volHalfLife);
    BacktestEngine eng(std::move(latency), std::move(strat), fees);
    eng.set_l2_reader(std::make_unique<CsvL2Reader>(lobPath));
    eng.set_trades_reader(std::make_unique<CsvTradesReader>(trdPath));
    eng.run();

    const auto& execs = eng.executions();
    const auto& eq = eng.equity_series();
    const auto& ts = eng.equity_ts_series();
    const auto& inv = eng.inventory_series();

    {
        std::vector<std::string> rows;
        rows.reserve(execs.size());
        for (const auto& e : execs)
            rows.push_back(std::to_string(e.ts) + ","
                           + (e.side == Side::Buy ? "buy" : "sell") + ","
                           + std::to_string(e.qty) + ","
                           + std::to_string(e.price) + ","
                           + std::to_string(e.fee));
        write_csv("executions.csv", "ts,side,qty,price,fee", rows);
    }
    {
        std::vector<std::string> rows;
        const std::size_t n = std::min({eq.size(), ts.size(), inv.size()});
        rows.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
            rows.push_back(std::to_string(ts[i]) + ","
                           + std::to_string(eq[i]) + ","
                           + std::to_string(inv[i]));
        write_csv("equity.csv", "ts,equity,inventory", rows);
    }

    const double spY = eng.estimated_steps_per_year();

    RealizedPnL realizedPnL(execs);
    UnrealizedPnL unrealizedPnL(eq);
    Turnover turnover(execs);
    MaxAbsInventory maxInv(inv);
    TimeWeightedAvgInventory twapInv(inv, ts);
    FillRatio fillRatio(eng.sent_count(), eng.filled_count());
    SharpeAnnualized sharpe(eng.unrealized_returns_series(), spY);
    SortinoAnnualized sortino(eng.unrealized_returns_series(), spY);
    MaxDrawdownPct mdd(eq);

    std::cout << "metric,value\n"
              << "realized_pnl," << realizedPnL.calculate() << "\n"
              << "unrealized_pnl," << unrealizedPnL.calculate() << "\n"
              << "turnover," << turnover.calculate() << "\n"
              << "max_inventory," << maxInv.calculate() << "\n"
              << "twap_inventory," << twapInv.calculate() << "\n"
              << "fill_ratio," << fillRatio.calculate() << "\n"
              << "sharpe," << sharpe.calculate() << "\n"
              << "sortino," << sortino.calculate() << "\n"
              << "max_drawdown_pct," << mdd.calculate() << "\n"
              << "fills," << execs.size() << "\n"
              << "k," << k << "\n"
              << "gamma," << gamma << "\n";

    return 0;
}
