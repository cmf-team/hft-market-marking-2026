#include "Backtester.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <cctype>
#include <vector>

namespace cmf
{
namespace
{

struct LobEvent
{
    std::int64_t timestamp = 0;
    double askPrice = 0.0;
    double askAmount = 0.0;
    double bidPrice = 0.0;
    double bidAmount = 0.0;
};

struct TradeEvent
{
    std::int64_t timestamp = 0;
    Side side = Side::None;
    double price = 0.0;
    double amount = 0.0;
};

struct ActiveOrder
{
    Side side = Side::None;
    double price = 0.0;
    double volume = 0.0;
    std::string rule;
};

struct Position
{
    Side direction = Side::None;
    double entryPrice = 0.0;
    double volume = 0.0;
    std::int64_t entryTimestamp = 0;
    std::string entryRule;
};

struct ClosedTrade
{
    std::int64_t enterTimestamp = 0;
    std::int64_t exitTimestamp = 0;
    std::string ruleToEnter;
    std::string ruleToExit;
    double enterPrice = 0.0;
    std::string enterDirection;
    double enterVolume = 0.0;
    double exitPrice = 0.0;
    std::string exitDirection;
    double exitPnl = 0.0;
};

struct MarketState
{
    bool hasLob = false;
    double ask = 0.0;
    double bid = 0.0;
    double askQty = 0.0;
    double bidQty = 0.0;
    double mid = 0.0;
    double spread = 0.0;
    std::int64_t timestamp = 0;
};


std::string trim(std::string s)
{
    const auto notSpace = [](unsigned char ch) { return std::isspace(ch) == 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

std::vector<std::string> splitCsvLine(const std::string& line)
{
    std::vector<std::string> out;
    std::string cell;
    bool inQuotes = false;
    for (char ch : line)
    {
        if (ch == '"')
        {
            inQuotes = !inQuotes;
        }
        else if (ch == ',' && !inQuotes)
        {
            out.push_back(trim(cell));
            cell.clear();
        }
        else
        {
            cell.push_back(ch);
        }
    }
    out.push_back(trim(cell));
    return out;
}

std::map<std::string, std::size_t> headerIndex(const std::vector<std::string>& cols)
{
    std::map<std::string, std::size_t> idx;
    for (std::size_t i = 0; i < cols.size(); ++i)
    {
        idx[cols[i]] = i;
    }
    return idx;
}

std::string getCell(const std::vector<std::string>& row, const std::map<std::string, std::size_t>& idx, const std::string& name)
{
    const auto it = idx.find(name);
    if (it == idx.end())
    {
        throw std::runtime_error("CSV column not found: " + name);
    }
    if (it->second >= row.size())
    {
        return {};
    }
    return row[it->second];
}

std::int64_t parseI64(const std::string& s)
{
    return static_cast<std::int64_t>(std::stoll(s));
}

double parseDouble(const std::string& s)
{
    if (s.empty())
    {
        return 0.0;
    }
    return std::stod(s);
}

Side parseSide(const std::string& s)
{
    if (s == "buy" || s == "Buy" || s == "BUY")
    {
        return Side::Buy;
    }
    if (s == "sell" || s == "Sell" || s == "SELL")
    {
        return Side::Sell;
    }
    return Side::None;
}

std::string sideToString(Side side)
{
    switch (side)
    {
    case Side::Buy:
        return "Buy";
    case Side::Sell:
        return "Sell";
    default:
        return "None";
    }
}

std::vector<LobEvent> loadLob(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
    {
        throw std::runtime_error("Cannot open LOB CSV: " + path);
    }

    std::string line;
    if (!std::getline(in, line))
    {
        throw std::runtime_error("LOB CSV is empty: " + path);
    }
    const auto idx = headerIndex(splitCsvLine(line));

    std::vector<LobEvent> events;
    events.reserve(100000);
    while (std::getline(in, line))
    {
        if (line.empty())
        {
            continue;
        }
        const auto row = splitCsvLine(line);
        LobEvent e;
        e.timestamp = parseI64(getCell(row, idx, "local_timestamp"));
        e.askPrice = parseDouble(getCell(row, idx, "asks[0].price"));
        e.askAmount = parseDouble(getCell(row, idx, "asks[0].amount"));
        e.bidPrice = parseDouble(getCell(row, idx, "bids[0].price"));
        e.bidAmount = parseDouble(getCell(row, idx, "bids[0].amount"));
        events.push_back(e);
    }
    return events;
}

std::vector<TradeEvent> loadTrades(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
    {
        throw std::runtime_error("Cannot open trades CSV: " + path);
    }

    std::string line;
    if (!std::getline(in, line))
    {
        throw std::runtime_error("Trades CSV is empty: " + path);
    }
    const auto idx = headerIndex(splitCsvLine(line));

    std::vector<TradeEvent> events;
    events.reserve(1200000);
    while (std::getline(in, line))
    {
        if (line.empty())
        {
            continue;
        }
        const auto row = splitCsvLine(line);
        TradeEvent e;
        e.timestamp = parseI64(getCell(row, idx, "local_timestamp"));
        e.side = parseSide(getCell(row, idx, "side"));
        e.price = parseDouble(getCell(row, idx, "price"));
        e.amount = parseDouble(getCell(row, idx, "amount"));
        events.push_back(e);
    }
    return events;
}

double roundDownToTick(double price, double tick)
{
    return std::floor((price / tick) + 1e-9) * tick;
}

double roundUpToTick(double price, double tick)
{
    return std::ceil((price / tick) - 1e-9) * tick;
}

double inferTickSize(const std::vector<LobEvent>& lob)
{
    double best = std::numeric_limits<double>::max();
    const std::size_t maxRows = std::min<std::size_t>(lob.size(), 5000U);
    for (std::size_t i = 0; i < maxRows; ++i)
    {
        const double spread = lob[i].askPrice - lob[i].bidPrice;
        if (spread > 0.0)
        {
            best = std::min(best, spread);
        }
    }
    if (!std::isfinite(best) || best == std::numeric_limits<double>::max())
    {
        return 0.000001;
    }
    return best;
}

std::string timestampToIsoUtc(std::int64_t micros)
{
    const std::time_t sec = static_cast<std::time_t>(micros / 1'000'000);
    const int microPart = static_cast<int>(micros % 1'000'000);
    std::tm tmUtc{};
#ifdef _WIN32
    gmtime_s(&tmUtc, &sec);
#else
    gmtime_r(&sec, &tmUtc);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmUtc, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(6) << std::setfill('0') << microPart;
    return oss.str();
}

bool isTradeFill(const ActiveOrder& order, const TradeEvent& trade)
{
    if (order.side == Side::Buy)
    {
        return trade.side == Side::Sell && trade.price <= order.price;
    }
    if (order.side == Side::Sell)
    {
        return trade.side == Side::Buy && trade.price >= order.price;
    }
    return false;
}

bool isLobOverlapFill(const ActiveOrder& order, const MarketState& market)
{
    if (!market.hasLob)
    {
        return false;
    }
    if (order.side == Side::Buy)
    {
        return market.ask <= order.price;
    }
    if (order.side == Side::Sell)
    {
        return market.bid >= order.price;
    }
    return false;
}

double entryFillPrice(const ActiveOrder& order, const std::optional<TradeEvent>& trade)
{
    if (trade.has_value())
    {
        return order.price;
    }
    return order.price;
}

ClosedTrade closePosition(const Position& pos, std::int64_t exitTimestamp, const std::string& rule, double exitPrice)
{
    ClosedTrade out;
    out.enterTimestamp = pos.entryTimestamp;
    out.exitTimestamp = exitTimestamp;
    out.ruleToEnter = pos.entryRule;
    out.ruleToExit = rule;
    out.enterPrice = pos.entryPrice;
    out.enterDirection = sideToString(pos.direction);
    out.enterVolume = pos.volume;
    out.exitPrice = exitPrice;
    out.exitDirection = sideToString(pos.direction == Side::Buy ? Side::Sell : Side::Buy);

    if (pos.direction == Side::Buy)
    {
        out.exitPnl = (exitPrice - pos.entryPrice) * pos.volume;
    }
    else if (pos.direction == Side::Sell)
    {
        out.exitPnl = (pos.entryPrice - exitPrice) * pos.volume;
    }
    return out;
}

void writeClosedTrades(const std::string& path, const std::vector<ClosedTrade>& trades)
{
    std::ofstream out(path);
    if (!out)
    {
        throw std::runtime_error("Cannot create output CSV: " + path);
    }

    out << "enter_datetime,enter_timestamp,exit_timestamp,exit_datetime,rule_to_enter,rule_to_exit,"
           "enter_price,enter_direction,enter_volume,exit_price,exit_direction,exit_pnl\n";
    out << std::fixed << std::setprecision(10);
    for (const auto& t : trades)
    {
        out << timestampToIsoUtc(t.enterTimestamp) << ',' << t.enterTimestamp << ',' << t.exitTimestamp << ','
            << timestampToIsoUtc(t.exitTimestamp) << ',' << t.ruleToEnter << ',' << t.ruleToExit << ','
            << t.enterPrice << ',' << t.enterDirection << ',' << t.enterVolume << ',' << t.exitPrice << ','
            << t.exitDirection << ',' << t.exitPnl << '\n';
    }
}

void printUsage()
{
    std::cout << "Usage:\n"
              << "  hft-market-making --lob lob1.csv --trades Trades1.csv --out closed_trades.csv [options]\n\n"
              << "Options:\n"
              << "  --center mid|reservation       Quote center. Default: reservation\n"
              << "  --fill-mode overlap|trade_only Fill by LOB/trade overlap or only trades. Default: overlap\n"
              << "  --volume N                     Order volume. Default: 1\n"
              << "  --quote-half-spread-ticks N    Quote distance from center. Default: 1\n"
              << "  --tp-ticks N                   Take-profit ticks. Default: 2\n"
              << "  --sl-ticks N                   Stop-loss ticks. Default: 4\n"
              << "  --max-hold-us N                Max holding time in microseconds. Default: 3000000\n"
              << "  --gamma N                      Risk aversion for reservation center. Default: 0.1\n"
              << "  --sigma N                      Price volatility per sqrt(second). Default: 0.00001\n"
              << "  --horizon-sec N                Reservation horizon. Default: 3\n"
              << "  --tick N                       Tick size. Default: infer from LOB\n";
}

std::optional<ActiveOrder> makeEntryOrder(Side side, double center, double halfSpreadTicks, double tick, double volume, const std::string& rule)
{
    if (tick <= 0.0 || volume <= 0.0)
    {
        return std::nullopt;
    }
    ActiveOrder order;
    order.side = side;
    order.volume = volume;
    order.rule = rule;
    if (side == Side::Buy)
    {
        order.price = roundDownToTick(center - halfSpreadTicks * tick, tick);
    }
    else if (side == Side::Sell)
    {
        order.price = roundUpToTick(center + halfSpreadTicks * tick, tick);
    }
    else
    {
        return std::nullopt;
    }
    return order;
}

std::optional<ClosedTrade> checkExit(const Position& pos,
                                     const MarketState& market,
                                     const BacktestConfig& cfg,
                                     double tick,
                                     std::int64_t now,
                                     const std::optional<TradeEvent>& trade)
{
    if (pos.direction == Side::None)
    {
        return std::nullopt;
    }

    const double tp = pos.direction == Side::Buy ? pos.entryPrice + cfg.takeProfitTicks * tick : pos.entryPrice - cfg.takeProfitTicks * tick;
    const double sl = pos.direction == Side::Buy ? pos.entryPrice - cfg.stopLossTicks * tick : pos.entryPrice + cfg.stopLossTicks * tick;

    if (trade.has_value())
    {
        if (pos.direction == Side::Buy && trade->side == Side::Buy && trade->price >= tp)
        {
            return closePosition(pos, now, "take_profit", tp);
        }
        if (pos.direction == Side::Sell && trade->side == Side::Sell && trade->price <= tp)
        {
            return closePosition(pos, now, "take_profit", tp);
        }
        if (pos.direction == Side::Buy && trade->price <= sl)
        {
            return closePosition(pos, now, "stop_loss", trade->price);
        }
        if (pos.direction == Side::Sell && trade->price >= sl)
        {
            return closePosition(pos, now, "stop_loss", trade->price);
        }
    }

    if (market.hasLob && cfg.fillMode == "overlap")
    {
        if (pos.direction == Side::Buy && market.bid >= tp)
        {
            return closePosition(pos, now, "take_profit", tp);
        }
        if (pos.direction == Side::Sell && market.ask <= tp)
        {
            return closePosition(pos, now, "take_profit", tp);
        }
        if (pos.direction == Side::Buy && market.mid <= sl)
        {
            return closePosition(pos, now, "stop_loss", market.bid);
        }
        if (pos.direction == Side::Sell && market.mid >= sl)
        {
            return closePosition(pos, now, "stop_loss", market.ask);
        }
    }

    if (now - pos.entryTimestamp >= cfg.maxHoldMicros && market.hasLob)
    {
        const double exitPrice = pos.direction == Side::Buy ? market.bid : market.ask;
        return closePosition(pos, now, "by_time_duration", exitPrice);
    }

    return std::nullopt;
}

} // namespace

BacktestConfig parseBacktestArgs(int argc, const char* argv[])
{
    BacktestConfig cfg;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const auto next = [&]() -> std::string {
            if (i + 1 >= argc)
            {
                throw std::runtime_error("Missing value after " + arg);
            }
            ++i;
            return argv[i];
        };

        if (arg == "--help" || arg == "-h")
        {
            printUsage();
            std::exit(0);
        }
        if (arg == "--lob")
        {
            cfg.lobCsvPath = next();
        }
        else if (arg == "--trades")
        {
            cfg.tradesCsvPath = next();
        }
        else if (arg == "--out")
        {
            cfg.outputCsvPath = next();
        }
        else if (arg == "--center")
        {
            cfg.centerMode = next();
        }
        else if (arg == "--fill-mode")
        {
            cfg.fillMode = next();
        }
        else if (arg == "--volume")
        {
            cfg.orderVolume = parseDouble(next());
        }
        else if (arg == "--quote-half-spread-ticks")
        {
            cfg.quoteHalfSpreadTicks = parseDouble(next());
        }
        else if (arg == "--tp-ticks")
        {
            cfg.takeProfitTicks = parseDouble(next());
        }
        else if (arg == "--sl-ticks")
        {
            cfg.stopLossTicks = parseDouble(next());
        }
        else if (arg == "--max-hold-us")
        {
            cfg.maxHoldMicros = parseI64(next());
        }
        else if (arg == "--gamma")
        {
            cfg.riskAversion = parseDouble(next());
        }
        else if (arg == "--sigma")
        {
            cfg.volatilityPerSqrtSecond = parseDouble(next());
        }
        else if (arg == "--horizon-sec")
        {
            cfg.timeHorizonSeconds = parseDouble(next());
        }
        else if (arg == "--tick")
        {
            cfg.tickSize = parseDouble(next());
        }
        else
        {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }


    // C# equivalent: var path = !string.IsNullOrEmpty(cfg.lobCsvPath) ? cfg.lobCsvPath : "lob1.csv";
    std::string path = !cfg.lobCsvPath.empty() ? cfg.lobCsvPath : "lob1.csv";
    
    /*
    std::string lob_path = R"(S:\YandexDisk\CMF\lob1.csv)";
std::string trades_path = R"(S:\YandexDisk\CMF\Trades1.csv)";
std::string out_path = R"(S:\YandexDisk\CMF\closed_trades.csv)";
    */


    if (cfg.lobCsvPath.empty())
    {
        cfg.lobCsvPath = cfg.dir_path + "lob1.csv";
    }
    if (cfg.tradesCsvPath.empty())
    {
        cfg.tradesCsvPath = cfg.dir_path + "Trades1.csv";
    }
    if (cfg.outputCsvPath.empty()) //  || cfg.outputCsvPath == "closed_trades.csv"
    {
        cfg.outputCsvPath = cfg.dir_path + "closed_trades.csv";
    }


    if (cfg.centerMode != "mid" && cfg.centerMode != "reservation")
    {
        throw std::runtime_error("--center must be mid or reservation");
    }
    if (cfg.fillMode != "overlap" && cfg.fillMode != "trade_only")
    {
        throw std::runtime_error("--fill-mode must be overlap or trade_only");
    }
    return cfg;
}

BacktestSummary runBacktest(const BacktestConfig& config)
{
    std::cout << "Loading LOB: " << config.lobCsvPath << '\n';
    auto lob = loadLob(config.lobCsvPath);
    std::cout << "Loading trades: " << config.tradesCsvPath << '\n';
    auto trades = loadTrades(config.tradesCsvPath);

    std::sort(lob.begin(), lob.end(), [](const LobEvent& a, const LobEvent& b) { return a.timestamp < b.timestamp; });
    std::sort(trades.begin(), trades.end(), [](const TradeEvent& a, const TradeEvent& b) { return a.timestamp < b.timestamp; });

    const double tick = config.tickSize > 0.0 ? config.tickSize : inferTickSize(lob);
    std::cout << std::fixed << std::setprecision(10) << "Tick size: " << tick << '\n';

    BacktestSummary summary;
    summary.lobRows = lob.size();
    summary.tradeRows = trades.size();

    MarketState market;
    std::vector<ClosedTrade> closed;
    std::optional<Position> position;
    std::optional<ActiveOrder> bidOrder;
    std::optional<ActiveOrder> askOrder;

    std::size_t iLob = 0;
    std::size_t iTrade = 0;

    const auto placeEntryQuotes = [&]() {
        if (!market.hasLob || position.has_value())
        {
            return;
        }
        double center = market.mid;
        if (config.centerMode == "reservation")
        {
            const double inv = 0.0;
            center = market.mid - inv * config.riskAversion * config.volatilityPerSqrtSecond * config.volatilityPerSqrtSecond * config.timeHorizonSeconds;
        }
        bidOrder = makeEntryOrder(Side::Buy, center, config.quoteHalfSpreadTicks, tick, config.orderVolume, config.centerMode + "_symmetric_bid");
        askOrder = makeEntryOrder(Side::Sell, center, config.quoteHalfSpreadTicks, tick, config.orderVolume, config.centerMode + "_symmetric_ask");
    };

    const auto openPositionFromOrder = [&](const ActiveOrder& order, std::int64_t ts, const std::optional<TradeEvent>& trade) {
        Position pos;
        pos.direction = order.side;
        pos.entryPrice = entryFillPrice(order, trade);
        pos.volume = order.volume;
        pos.entryTimestamp = ts;
        pos.entryRule = order.rule;
        position = pos;
        bidOrder.reset();
        askOrder.reset();
    };

    while (iLob < lob.size() || iTrade < trades.size())
    {
        const bool useLob = iLob < lob.size() && (iTrade >= trades.size() || lob[iLob].timestamp <= trades[iTrade].timestamp);

        if (useLob)
        {
            const auto& e = lob[iLob++];
            market.hasLob = true;
            market.timestamp = e.timestamp;
            market.ask = e.askPrice;
            market.bid = e.bidPrice;
            market.askQty = e.askAmount;
            market.bidQty = e.bidAmount;
            market.mid = (market.ask + market.bid) * 0.5;
            market.spread = market.ask - market.bid;

            if (position.has_value())
            {
                auto maybeClosed = checkExit(*position, market, config, tick, e.timestamp, std::nullopt);
                if (maybeClosed.has_value())
                {
                    closed.push_back(*maybeClosed);
                    summary.totalPnl += maybeClosed->exitPnl;
                    position.reset();
                }
            }

            if (!position.has_value())
            {
                placeEntryQuotes();
                if (config.fillMode == "overlap")
                {
                    if (bidOrder.has_value() && isLobOverlapFill(*bidOrder, market))
                    {
                        openPositionFromOrder(*bidOrder, e.timestamp, std::nullopt);
                    }
                    else if (askOrder.has_value() && isLobOverlapFill(*askOrder, market))
                    {
                        openPositionFromOrder(*askOrder, e.timestamp, std::nullopt);
                    }
                }
            }
        }
        else
        {
            const auto& e = trades[iTrade++];

            if (position.has_value())
            {
                auto maybeClosed = checkExit(*position, market, config, tick, e.timestamp, e);
                if (maybeClosed.has_value())
                {
                    closed.push_back(*maybeClosed);
                    summary.totalPnl += maybeClosed->exitPnl;
                    position.reset();
                }
            }

            if (!position.has_value())
            {
                placeEntryQuotes();
                if (bidOrder.has_value() && isTradeFill(*bidOrder, e))
                {
                    openPositionFromOrder(*bidOrder, e.timestamp, e);
                }
                else if (askOrder.has_value() && isTradeFill(*askOrder, e))
                {
                    openPositionFromOrder(*askOrder, e.timestamp, e);
                }
            }
        }
    }

    if (position.has_value() && market.hasLob)
    {
        const double exitPrice = position->direction == Side::Buy ? market.bid : market.ask;
        auto finalClosed = closePosition(*position, market.timestamp, "end_of_data", exitPrice);
        summary.totalPnl += finalClosed.exitPnl;
        closed.push_back(finalClosed);
    }

    writeClosedTrades(config.outputCsvPath, closed);
    summary.closedTrades = closed.size();

    std::cout << "Closed trades saved: " << config.outputCsvPath << '\n';
    std::cout << "Closed trades: " << summary.closedTrades << '\n';
    std::cout << "Total PnL: " << summary.totalPnl << '\n';
    return summary;
}

} // namespace cmf
