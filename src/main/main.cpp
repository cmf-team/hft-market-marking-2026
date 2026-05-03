#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "avellaneda_stoikov_strategy.hpp"
#include "config.hpp"
#include "csv_reader.hpp"
#include "exchange.hpp"
#include "replay_engine.hpp"
#include "report.hpp"
#include "simple_cycle_strategy.hpp"
#include "stats.hpp"

namespace {

std::unique_ptr<hft::IStrategy> make_strategy(const hft::BacktestConfig& config) {
    if (config.strategy == "simple_cycle") {
        return std::make_unique<hft::SimpleCycleStrategy>(
            config.order_qty, config.take_profit_bps, config.entry_refresh_us,
            config.max_position);
    }

    if (config.strategy == "avellaneda_stoikov" ||
        config.strategy == "as" ||
        config.strategy == "microprice_avellaneda_stoikov" ||
        config.strategy == "microprice_as") {
        hft::AvellanedaStoikovParams params;
        params.order_qty = config.order_qty;
        params.max_position = static_cast<double>(config.max_position);
        params.gamma = config.as_gamma;
        params.k = config.as_k;
        params.sigma = config.as_sigma;
        params.sigma_floor = config.as_sigma_floor;
        params.volatility_ewma_alpha = config.as_volatility_ewma_alpha;
        params.horizon_us = config.as_horizon_us;
        params.quote_refresh_us = config.as_quote_refresh_us;
        params.tick_size = config.as_tick_size;
        params.min_spread_ticks = config.as_min_spread_ticks;
        params.spread_multiplier = config.as_spread_multiplier;
        params.use_microprice =
            config.as_use_microprice ||
            config.strategy == "microprice_avellaneda_stoikov" ||
            config.strategy == "microprice_as";
        params.microprice_alpha = config.as_microprice_alpha;
        return std::make_unique<hft::AvellanedaStoikovStrategy>(params);
    }

    throw std::runtime_error("Unknown strategy: " + config.strategy);
}

}

int main(int argc, char** argv) {
    try {

        const std::string config_path =
            argc > 1 ? argv[1] : "configs/sample_config.ini";
        const hft::BacktestConfig config = hft::load_config(config_path);


        hft::CsvLobReader lob_reader(config.lob_path);
        hft::CsvTradeReader trade_reader(config.trades_path);
        hft::ExchangeEmulator exchange(config.fill_on_touch);
        hft::BacktestStats stats(config.initial_cash);

        std::unique_ptr<hft::IStrategy> strategy = make_strategy(config);
        hft::ReplayEngine engine(config, *strategy, lob_reader, trade_reader, exchange,
                                 stats);

        engine.run();


        const std::string report =
            hft::build_report_markdown(config, stats, strategy->name());
        std::cout << report << "\n";

        if (!config.report_path.empty()) {
            if (!hft::write_report(config.report_path, report)) {
                std::cerr << "Warning: failed to write report to "
                          << config.report_path << "\n";
            }
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Backtest failed: " << ex.what() << "\n";
        return 1;
    }
}
