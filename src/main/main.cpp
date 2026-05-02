// main function for the hft-market-making app
// please, keep it minimalistic

#include "Config.hpp"
#include "bt_engine/BacktestEngine.hpp"
#include "bt_engine/data_reader/DataReader.hpp"
#include "trade_engine/strategy/StoikovMM.hpp"

#include <iostream>
#include <memory>

int main(int argc, const char* argv[])
{
    Config cfg = (argc > 1) ? Config::fromFile(argv[1]) : Config{};

    auto strategy = std::make_unique<StoikovMM>(
        cfg.gamma, cfg.sigma, cfg.k, cfg.T, cfg.order_size, cfg.use_microprice);
    BacktestEngine bt_engine;

    dataReader(cfg.lob_file, cfg.trades_file, bt_engine, *strategy);

    strategy->writeCsv(cfg.output_csv);
    strategy->writeReport(cfg.output_report);

    std::cerr << "PnL:      " << strategy->calculatePnl()      << "\n"
              << "Turnover: " << strategy->calculateTurnover() << "\n";

    return 0;
}
