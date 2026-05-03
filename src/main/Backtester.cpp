#include "main/Backtester.hpp"

#include "common/DataFeed.hpp"
#include "common/Exchange.hpp"
#include "common/PnLTracker.hpp"
#include "common/Strategy.hpp"
#include "main/AvellanedaStoikov.hpp"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

namespace cmf
{

BacktestArgs parseArgs(int argc, const char* argv[])
{
    BacktestArgs a;
    for (int i = 1; i < argc; ++i)
    {
        std::string k = argv[i];
        auto need = [&](const char* name) -> std::string
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value for " << name << "\n";
                std::exit(1);
            }
            return std::string(argv[++i]);
        };

        if (k == "--trades")
            a.trades_path = need("--trades");
        else if (k == "--lob")
            a.lob_path = need("--lob");
        else if (k == "--output")
            a.output_path = need("--output");
        else if (k == "--strategy")
            a.strategy = need("--strategy");
        else if (k == "--start")
            a.start_time = std::stoll(need("--start"));
        else if (k == "--end")
            a.end_time = std::stoll(need("--end"));
        else if (k == "--mtm-us")
            a.mtm_interval_us = std::stoll(need("--mtm-us"));
        else if (k == "--no-partial")
            a.partial_fills = false;
        else if (k == "--max-events")
            a.max_events = std::stoull(need("--max-events"));
        else if (k == "--gamma")
            a.as.gamma = std::stod(need("--gamma"));
        else if (k == "--k")
            a.as.k = std::stod(need("--k"));
        else if (k == "--horizon")
            a.as.horizon_seconds = std::stod(need("--horizon"));
        else if (k == "--fixed-horizon")
            a.as.fixed_horizon = true;
        else if (k == "--qty")
            a.as.order_qty = std::stod(need("--qty"));
        else if (k == "--max-inv")
            a.as.max_inventory = std::stod(need("--max-inv"));
        else if (k == "--sigma-window")
            a.as.sigma_window = std::stoi(need("--sigma-window"));
        else if (k == "--sigma")
            a.as.sigma_override = std::stod(need("--sigma"));
        else if (k == "--requote-us")
            a.as.requote_us = std::stoi(need("--requote-us"));
        else if (k == "--verbose")
            a.as.verbose = true;
        else
        {
            std::cerr << "Unknown flag: " << k << "\n";
            std::exit(1);
        }
    }
    return a;
}

namespace
{

std::unique_ptr<Strategy> makeStrategy(const BacktestArgs& a)
{
    if (a.strategy == "none")
        return nullptr;
    if (a.strategy == "as" || a.strategy == "avellaneda_stoikov")
        return std::make_unique<AvellanedaStoikov>(a.as);
    std::cerr << "Unknown strategy: " << a.strategy << "\n";
    std::exit(1);
}

} // namespace

int runBacktest(const BacktestArgs& args)
{
    std::cout << "Data:\n"
              << "  trades: " << args.trades_path << "\n"
              << "  lob:    " << args.lob_path << "\n"
              << "  partial fills: " << (args.partial_fills ? "on" : "off") << "\n"
              << "  strategy: " << args.strategy << "\n";

    DataFeed feed(args.trades_path, args.lob_path);
    Exchange exchange(args.partial_fills);
    PnLTracker pnl;
    auto strategy = makeStrategy(args);

    EventType etype;
    Trade trade;
    LOBSnapshot lob;
    MicroTime last_mtm = 0;
    std::uint64_t n_trades = 0, n_lob = 0;

    auto wall_start = std::chrono::steady_clock::now();

    auto drain_fills = [&]()
    {
        for (const auto& fill : exchange.pollFills())
        {
            pnl.onFill(fill);
            if (strategy)
                strategy->onFill(fill);
        }
    };

    while (feed.next(etype, trade, lob))
    {
        MicroTime ts = (etype == EventType::Trade) ? trade.timestamp : lob.timestamp;
        if (ts < args.start_time)
            continue;
        if (ts > args.end_time)
            break;
        if (args.max_events && feed.eventsProcessed() > args.max_events)
            break;

        if (etype == EventType::Trade)
        {
            exchange.onTrade(trade);
            drain_fills();
            if (strategy)
                strategy->onTrade(trade, exchange);
            drain_fills();
            ++n_trades;
        }
        else
        {
            exchange.onLobUpdate(lob);
            drain_fills();
            if (strategy)
                strategy->onLobUpdate(lob, exchange);
            drain_fills();
            ++n_lob;

            if (ts - last_mtm >= args.mtm_interval_us)
            {
                pnl.markToMarket(exchange.midPrice(), ts);
                last_mtm = ts;
            }
        }

        if (feed.eventsProcessed() % 2'000'000 == 0)
        {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - wall_start).count();
            std::cout << "\r  Events: " << feed.eventsProcessed() / 1'000'000
                      << "M  Inv: " << std::fixed << std::setprecision(0) << pnl.inventory()
                      << "  PnL: " << std::setprecision(6) << pnl.totalPnl()
                      << "  Fills: " << pnl.stats().total_fills << "  "
                      << std::setprecision(1) << elapsed << "s" << std::flush;
        }
    }

    if (exchange.hasMarketData())
        pnl.markToMarket(exchange.midPrice(), exchange.currentTime());

    auto wall_end = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(wall_end - wall_start).count();
    std::cout << "\n\n  Completed in " << std::fixed << std::setprecision(1) << secs << "s\n"
              << "  " << feed.eventsProcessed() << " events (" << n_trades << " trades, "
              << n_lob << " LOB)\n"
              << "  Throughput: " << std::setprecision(0) << feed.eventsProcessed() / secs
              << " events/s\n";

    pnl.printReport();
    pnl.saveEquityCurve(args.output_path);
    return 0;
}

} // namespace cmf
