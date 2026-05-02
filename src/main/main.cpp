// main function for the hft-market-making app
// please, keep it minimalistic

#include "analytics/pnl_tracker.hpp"
#include "engine/backtester.hpp"
#include "exec/matching_engine.hpp"
#include "strategy/strategies/market_maker.hpp"

#include <chrono>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    try
    {
        std::string book_path   = (argc > 1) ? argv[1] : "data/lob.csv";
        std::string trades_path = (argc > 2) ? argv[2] : "data/trades.csv";
        std::string output_dir  = (argc > 3) ? argv[3] : "results";

        std::cout << "book:   " << book_path   << '\n'
                  << "trades: " << trades_path << '\n'
                  << "output: " << output_dir  << "/pnl_timeseries.csv\n";

        cmf::Backtester     bt(book_path, trades_path);
        cmf::MatchingEngine me;
        cmf::MarketMaker    mm;
        cmf::PnLTracker     pnl;

        mm.set_matching_engine(&me);
        bt.enable_csv_output(output_dir + "/pnl_timeseries.csv");

        auto t0 = std::chrono::steady_clock::now();
        bt.run(&mm, &me, &pnl);
        auto t1 = std::chrono::steady_clock::now();
        const double secs = std::chrono::duration<double>(t1 - t0).count();

        const auto& s = bt.stream();
        const std::size_t total_events = s.book_events() + s.trade_events();

        std::cout << "\n--- summary ---\n"
                  << "total events:     " << total_events << '\n'
                  << "  book updates:   " << s.book_events()  << '\n'
                  << "  trades:         " << s.trade_events() << '\n'
                  << "last ts:          " << bt.last_timestamp() << '\n'
                  << "wall (s):         " << secs << '\n'
                  << "throughput (M/s): " << (secs > 0 ? total_events / secs / 1e6 : 0.0) << '\n';

        std::cout << "\n--- strategy ---\n"
                  << "requotes:         " << mm.requote_count() << '\n'
                  << "active:           " << (mm.active() ? "yes" : "no") << '\n'
                  << "final bid:        " << mm.bid_price() << " x " << mm.bid_size() << '\n'
                  << "final ask:        " << mm.ask_price() << " x " << mm.ask_size() << '\n';

        std::cout << "\n--- pnl ---\n"
                  << "fills (me):       " << me.fills_emitted() << '\n'
                  << "fills:            " << pnl.fill_count() << '\n'
                  << "volume:           " << pnl.volume()     << '\n'
                  << "position:         " << pnl.position()   << '\n'
                  << "cash:             " << pnl.cash()       << '\n'
                  << "equity:           " << pnl.equity()     << '\n'
                  << "max equity:       " << pnl.max_equity() << '\n'
                  << "min equity:       " << pnl.min_equity() << '\n'
                  << "max drawdown:     " << pnl.max_drawdown() << '\n';
    }
    catch (std::exception& ex)
    {
        std::cerr << "HFT market-making app threw an exception: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
