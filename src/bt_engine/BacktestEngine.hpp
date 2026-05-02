#pragma once
#include "data_reader/MarketTradeReader.hpp"
#include "trade_engine/strategy/StrategyBase.hpp"

#include <algorithm>
#include <vector>

class BacktestEngine {
    std::vector<Order> openOrders;

public:
    void applyOrders(const std::vector<Order>& orders) {
        for (const Order& order : orders) {
            if (order.eventType == "cancel") {
                openOrders.erase(
                    std::remove_if(openOrders.begin(), openOrders.end(),
                        [&](const Order& o) { return o.id == order.id; }),
                    openOrders.end());
            } else {
                openOrders.push_back(order);
            }
        }
    }

    std::vector<Order> reactToMarketTrade(const MarketTradeRow& trade) {
        std::vector<Order> fills;
        double tradeRemaining = trade.amount;

        for (Order& order : openOrders) {
            if (tradeRemaining <= 0.0) break;

            bool matches =
                (trade.side == "sell" && order.side == "buy"  && order.price >= trade.price) ||
                (trade.side == "buy"  && order.side == "sell" && order.price <= trade.price);

            if (matches) {
                double remaining = order.amount - order.filledAmount;
                double fillQty   = std::min(remaining, tradeRemaining);
                order.filledAmount += fillQty;
                tradeRemaining     -= fillQty;

                Order fill    = order;
                fill.amount   = fillQty;
                fill.eventType = "fill";
                fill.timestamp = trade.local_timestamp;
                fills.push_back(fill);
            }
        }

        openOrders.erase(
            std::remove_if(openOrders.begin(), openOrders.end(),
                [](const Order& o) { return o.filledAmount >= o.amount; }),
            openOrders.end());

        return fills;
    }
};