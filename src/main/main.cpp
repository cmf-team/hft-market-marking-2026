#include "BacktestEngine.hpp"
#include "Strategy.hpp"
#include "common/BasicTypes.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>

using namespace cmf;

std::vector<std::string> splitCSV(const std::string& line)
{
    std::vector<std::string> result;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ','))
    {
        result.push_back(item);
    }
    return result;
}

std::unordered_map<std::string, int> getHeaderMap(const std::string& header)
{
    auto tokens = splitCSV(header);
    std::unordered_map<std::string, int> map;
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        map[tokens[i]] = i;
    }
    return map;
}

void loadLOB(const std::string& path, std::vector<MarketEvent>& events)
{
    std::ifstream file(path);
    std::string line;
    if (!std::getline(file, line))
        return;

    auto hmap = getHeaderMap(line);
    if (!hmap.count("local_timestamp") || !hmap.count("asks[0].price"))
    {
        std::cerr << "Invalid LOB format\n";
        return;
    }

    size_t t_idx = hmap["local_timestamp"], ap_idx = hmap["asks[0].price"], as_idx = hmap["asks[0].amount"];
    size_t bp_idx = hmap["bids[0].price"], bs_idx = hmap["bids[0].amount"];

    while (std::getline(file, line))
    {
        if (line.empty())
            continue;
        auto tokens = splitCSV(line);
        if (tokens.size() <= std::max({t_idx, ap_idx, as_idx, bp_idx, bs_idx}))
            continue;

        MarketEvent ev;
        ev.ts_recv = std::stoll(tokens[t_idx]);
        ev.type = EventType::Quote;
        ev.best_ask_price = std::stod(tokens[ap_idx]);
        ev.best_ask_size = std::stod(tokens[as_idx]);
        ev.best_bid_price = std::stod(tokens[bp_idx]);
        ev.best_bid_size = std::stod(tokens[bs_idx]);

        events.push_back(ev);
    }
}

void loadTrades(const std::string& path, std::vector<MarketEvent>& events)
{
    std::ifstream file(path);
    std::string line;
    if (!std::getline(file, line))
        return;

    auto hmap = getHeaderMap(line);
    if (!hmap.count("local_timestamp") || !hmap.count("price"))
    {
        std::cerr << "Invalid Trades format\n";
        return;
    }

    size_t t_idx = hmap["local_timestamp"], p_idx = hmap["price"], a_idx = hmap["amount"], s_idx = hmap["side"];

    while (std::getline(file, line))
    {
        if (line.empty())
            continue;
        auto tokens = splitCSV(line);
        if (tokens.size() <= std::max({t_idx, p_idx, a_idx, s_idx}))
            continue;

        MarketEvent ev;
        ev.ts_recv = std::stoll(tokens[t_idx]);
        ev.type = EventType::Trade;
        ev.trade_price = std::stod(tokens[p_idx]);
        ev.trade_size = std::stod(tokens[a_idx]);

        std::string side_str = tokens[s_idx];
        if (side_str == "B" || side_str == "buy" || side_str == "1")
            ev.trade_side = Side::Buy;
        else if (side_str == "S" || side_str == "sell" || side_str == "-1")
            ev.trade_side = Side::Sell;
        else
            ev.trade_side = Side::None;

        events.push_back(ev);
    }
}

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << " <lob.csv> <trades.csv>\n";
        return 1;
    }

    std::cout << "Loading datasets...\n";
    std::vector<MarketEvent> events;
    loadLOB(argv[1], events);
    loadTrades(argv[2], events);

    std::cout << "Sorting " << events.size() << " events chronologically...\n";
    std::sort(events.begin(), events.end());

    BacktestEngine engine_as2008;
    StrategyAS2008 strat_as2008(&engine_as2008);

    BacktestEngine engine_micro;
    StrategyMicroprice strat_micro(&engine_micro);

    std::cout << "Running simulations...\n\n";
    for (const auto& ev : events)
    {
        engine_as2008.processEvent(ev);
        if (ev.type == EventType::Quote)
            strat_as2008.updateQuotes();

        engine_micro.processEvent(ev);
        if (ev.type == EventType::Quote)
            strat_micro.updateQuotes();
    }

    engine_as2008.printReport("Avellaneda-Stoikov (2008) - Classic MidPrice");
    engine_micro.printReport("Avellaneda-Stoikov (2018) - Microprice");

    return 0;
}