#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

#include "strategies/avellaneda_stoikov_strategy.hpp"
#include "backtest/engine/engine.hpp"
#include "data_ingestion/data_types.hpp"
#include "data_ingestion/loaders/SimpleLoader.hpp"
#include "data_ingestion/parsers/csv/LOBCSVParser.hpp"
#include "data_ingestion/parsers/csv/TradeCSVParser.hpp"
#include "strategies/microprice_strategy.hpp"

using Clock = std::chrono::high_resolution_clock;

template <typename Strategy>
static void run(const std::string& data_dir, Strategy& strat) {
    backtest::BacktestEngine engine(strat);

    auto t0 = Clock::now();

    std::vector<data::Event> events;

    bt::SimpleLoader<data::parser::TradeCSVParser> trade_loader(data_dir + "/trades.csv", true);
    trade_loader.load([&](data::Trade t) {
        events.push_back({t.local_timestamp, data::EventType::Trade, t});
    });

    auto t1 = Clock::now();

    bt::SimpleLoader<data::parser::LOBCSVParser> lob_loader(data_dir + "/lob.csv", true);
    lob_loader.load([&](data::OrderBookSnapshot s) {
        events.push_back({s.local_timestamp, data::EventType::Snapshot, s});
    });

    auto t2 = Clock::now();

    std::sort(events.begin(), events.end());

    auto t3 = Clock::now();

    std::cout << "Loaded events: " << events.size()
              << " first_ts=" << events[0].local_timestamp << "\n";

    engine.run(events);

    auto t4 = Clock::now();

    auto stats = strat.calculate_analytics();

    auto dur = [](auto start, auto end) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    };

    std::cout << "PnL:    " << stats.pnl    << "\n";
    std::cout << "Sharpe: " << stats.sharpe  << "\n";
    std::cout << "Fills:  " << stats.trades  << "\n";

    std::cout << "Load trades:     " << dur(t0, t1) << " ms\n";
    std::cout << "Load orderbooks: " << dur(t1, t2) << " ms\n";
    std::cout << "Sort + merge:    " << dur(t2, t3) << " ms\n";
    std::cout << "Engine run:      " << dur(t3, t4) << " ms\n";
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: hft-market-making <data_dir> <strategy>\n"
                  << "  strategy: microprice | avellaneda-stoikov\n";
        return 1;
    }

    const std::string data_dir      = argv[1];
    const std::string strategy_name = argv[2];

    if (strategy_name == "microprice") {
        MicroPriceParams params;
        params.calib_snapshots   = 5000;
        params.n_imbal_buckets   = 10;
        params.n_spread_levels   = 5;
        params.order_qty         = 1.0;
        params.quote_half_spread = 0.0;
        params.max_inventory     = 10.0;
        params.quote_every       = 1;
        params.verbose           = false;
        MicroPriceStrategy strat(params);
        run(data_dir, strat);
    } else if (strategy_name == "avellaneda-stoikov") {
        ASParams params;
        params.gamma         = 0.1;
        params.k             = 1e5;   // scaled to asset price magnitude (~0.011)
        params.order_qty     = 1.0;
        params.vol_window    = 100;
        params.quote_every   = 1;
        params.max_inventory = 10.0;
        params.session_secs  = 600.0;
        params.verbose       = false;
        AvellanedaStoikovStrategy strat(params);
        run(data_dir, strat);
    } else {
        std::cerr << "Unknown strategy: " << strategy_name << "\n"
                  << "  strategy: microprice | avellaneda-stoikov\n";
        return 1;
    }

    return 0;
}
