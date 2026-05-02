// main function for the hft-market-making app
// please, keep it minimalistic

#include "analytics/pnl_tracker.hpp"
#include "engine/backtester.hpp"
#include "exec/matching_engine.hpp"
#include "strategy/strategies/avellaneda_stoikov.hpp"
#include "strategy/strategies/market_maker.hpp"

#include <chrono>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    try
    {
        std::string book_path = (argc > 1) ? argv[1] : "data/lob.csv";
        std::string trades_path = (argc > 2) ? argv[2] : "data/trades.csv";
        std::string output_dir = (argc > 3) ? argv[3] : "results";

        std::cout << "book:   " << book_path << '\n'
                  << "trades: " << trades_path << '\n'
                  << "output: " << output_dir << "/pnl_timeseries.csv\n";

        cmf::Backtester bt(book_path, trades_path);
        cmf::MatchingEngine me;
        cmf::PnLTracker pnl;
        //cmf::MarketMaker mm;

        cmf::AvellanedaStoikov::Params p;
        p.tick_size = 1e-7; // matches data: prices ~0.0110436, tick at 7th decimal
        p.gamma = 1;
        p.q_max = 1000.0;
        p.q_min = -1000.0;
        p.quote_size = 100.0;
        p.vol_window = 100;
        // vol_dt MUST share units with (session_end - ts). Timestamps are µs
        // and average gap between book events is ~5e8 ns (5.18e11 ns / 1.04M events),
        // so sigma is measured in returns/sqrt(µs) and matches T-t in µs.
        p.vol_dt = 5e8;
        p.min_half_spread_ticks = 2.0;

        cmf::AvellanedaStoikov mm(p);
        mm.set_matching_engine(&me);
        // Data spans ~1.722e15 -> 1.723e15 microseconds. Set session_end just past
        // the last observed timestamp so (T-t) stays positive and finite.
        mm.set_session_end(static_cast<cmf::NanoTime>(1'723'000'000'000'000LL));

        bt.enable_csv_output(output_dir + "/pnl_timeseries.csv");

        auto t0 = std::chrono::steady_clock::now();
        bt.run(&mm, &me, &pnl);
        auto t1 = std::chrono::steady_clock::now();
        const double secs = std::chrono::duration<double>(t1 - t0).count();

        const auto& s = bt.stream();
        const std::size_t total_events = s.book_events() + s.trade_events();

        std::cout << "\n--- summary ---\n"
                  << "total events:     " << total_events << '\n'
                  << "  book updates:   " << s.book_events() << '\n'
                  << "  trades:         " << s.trade_events() << '\n'
                  << "last ts:          " << bt.last_timestamp() << '\n'
                  << "wall (s):         " << secs << '\n'
                  << "throughput (M/s): " << (secs > 0 ? total_events / secs / 1e6 : 0.0) << '\n';

        std::cout << "\n--- strategy ---\n"
                  << "requotes:         " << mm.requote_count() << '\n'
                  << "cross skips:      " << mm.cross_skips() << '\n'
                  << "final inventory:  " << mm.inventory() << '\n'
                  << "final reservation:" << mm.reservation() << '\n'
                  << "final half-spread:" << mm.half_spread() << '\n'
                  << "final sigma:      " << mm.sigma() << '\n'
                  << "final bid:        " << mm.bid_price() << (mm.bid_active() ? " (active)" : " (off)") << '\n'
                  << "final ask:        " << mm.ask_price() << (mm.ask_active() ? " (active)" : " (off)") << '\n';

        std::cout << "\n--- pnl ---\n"
                  << "fills (me):       " << me.fills_emitted() << '\n'
                  << "fills:            " << pnl.fill_count() << '\n'
                  << "volume:           " << pnl.volume() << '\n'
                  << "position:         " << pnl.position() << '\n'
                  << "cash:             " << pnl.cash() << '\n'
                  << "equity:           " << pnl.equity() << '\n'
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
