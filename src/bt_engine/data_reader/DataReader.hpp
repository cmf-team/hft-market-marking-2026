#pragma once
#include "LimitOrderBookReader.hpp"
#include "MarketTradeReader.hpp"
#include "bt_engine/BacktestEngine.hpp"
#include "trade_engine/strategy/StrategyBase.hpp"

#include <fstream>
#include <iostream>
#include <string>

void dataReader(
    const std::string& filename_lob,
    const std::string& filename_trades,
    BacktestEngine& bt_engine,
    StrategyBase& strategy)
{
    std::ifstream fileLob(filename_lob);
    std::ifstream fileTrades(filename_trades);
    if (!fileLob.is_open() || !fileTrades.is_open()) {
        std::cerr << "Cannot open input files\n";
        return;
    }

    std::string header;
    std::getline(fileLob, header);
    std::getline(fileTrades, header);

    std::string lineLob, lineTrade;
    long long rowsLob = 0, rowsTrades = 0;

    bool hasLob   = bool(std::getline(fileLob,    lineLob));
    bool hasTrade = bool(std::getline(fileTrades, lineTrade));

    OrderBookRow   lob{};
    MarketTradeRow trade{};
    if (hasLob)   lob   = parseLOBSnapshot(lineLob);
    if (hasTrade) trade = parseMarketTrade(lineTrade);

    while (hasLob || hasTrade) {
        bool doLob = hasLob && (!hasTrade || lob.local_timestamp <= trade.local_timestamp);

        if (doLob) {
            auto orders = strategy.reactToLob(lob);
            if (!orders.empty()) bt_engine.applyOrders(orders);
            ++rowsLob;
            hasLob = bool(std::getline(fileLob, lineLob));
            if (hasLob) lob = parseLOBSnapshot(lineLob);
        } else {
            auto fills = bt_engine.reactToMarketTrade(trade);
            strategy.reactToExecution(fills);
            ++rowsTrades;
            hasTrade = bool(std::getline(fileTrades, lineTrade));
            if (hasTrade) trade = parseMarketTrade(lineTrade);
        }
    }

    std::cerr << "Total rows lob: " << rowsLob << ". Total rows trades: " << rowsTrades << "\n";
}
