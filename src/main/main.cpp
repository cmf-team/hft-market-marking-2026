#include "data_loader/csv_lob_queue.hpp"
#include "data_loader/csv_trades_queue.hpp"
#include "data_loader/run_config.hpp"
#include "data_loader/streaming_event_source.hpp"
#include "execution/backtest_engine.hpp"
#include "metrics/metrics_calculator.hpp"
#include "portfolio/portfolio.hpp"
#include "strategy/avellaneda_stoikov_strategy.hpp"

#include <iostream>

namespace {

constexpr double INITIAL_CASH = 10'000.0;
constexpr bool MATCH_ON_BOOK_CROSS = false;
constexpr auto FAIR_PRICE =
    hft::strategy::AvellanedaStoikovStrategy::FairPrice::Microprice;

}

int main(int argc, char *argv[]) {
  try {
    const auto cfg = hft::data::parseArgs(argc, argv);

    hft::data::CsvLobQueue lob_q(cfg.lob_path);
    hft::data::CsvTradesQueue trades_q(cfg.trades_path);
    hft::data::StreamingEventSource source(lob_q, trades_q);

    auto portfolio = hft::portfolio::Portfolio::create(INITIAL_CASH);

    hft::execution::BacktestEngine engine;
    hft::execution::BacktestConfig config;
    config.match_on_book_cross = MATCH_ON_BOOK_CROSS;

    engine.setConfig(config);
    engine.setPortfolio(portfolio);

    hft::strategy::AvellanedaStoikovStrategy strategy({}, FAIR_PRICE);

    if (!engine.run(source, strategy)) {
      std::cerr << "Backtest failed\n";
      return 1;
    }

    hft::metrics::MetricsCalculator metrics;

    std::cout << "\n=== RESULTS ===\n";
    std::cout << "strategy = microprice-as\n";
    for (const auto &[name, value] : metrics.calculateAll(*portfolio)) {
      std::cout << name << " = " << value << '\n';
    }

    return 0;

  } catch (const std::exception &e) {
    std::cerr << "error: " << e.what() << '\n';
    return 1;
  }
}
