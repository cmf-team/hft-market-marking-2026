#pragma once

#include "engine/Types.hpp"
#include <algorithm>

namespace cmf
{

class OrderBook
{
public:
    // VWAP fill across visible depth; residual takes last level price.
    static ExecReport execute_market(const Order& order, const L2Snapshot& book)
    {
        const bool isBuy = order.side == Side::Buy;
        const auto& levels = isBuy ? book.asks : book.bids;
        double toFill = order.amount;
        double cost = 0.0;
        double filled = 0.0;
        for (const auto& lvl : levels)
        {
            if (toFill <= 0.0)
                break;
            const double take = std::min(toFill, lvl.amount);
            cost += take * lvl.price;
            filled += take;
            toFill -= take;
        }
        if (toFill > 0.0 && !levels.empty())
        {
            cost += toFill * levels.back().price;
            filled += toFill;
        }
        return {order.id, filled, filled > 0.0 ? cost / filled : 0.0, book.ts};
    }

    static bool limit_crossed(const Order& order, const L2Snapshot& book)
    {
        if (order.type != OrderType::Limit)
            return false;
        if (order.side == Side::Buy)
            return !book.asks.empty() && book.asks.front().price <= order.price;
        return !book.bids.empty() && book.bids.front().price >= order.price;
    }

    // Passive maker fill at own limit price, qty capped by contraQty.
    static ExecReport passive_fill(const Order& order, double contraQty, NanoTime ts)
    {
        const double qty = std::min(order.amount, contraQty);
        return {order.id, qty, order.price, ts};
    }

    // Aggressor taker fill at best contra price.
    static ExecReport aggressor_fill(const Order& order, const L2Snapshot& book)
    {
        const auto& contra = (order.side == Side::Buy) ? book.asks : book.bids;
        if (contra.empty())
            return {order.id, 0.0, 0.0, book.ts};
        return {order.id, order.amount, contra.front().price, book.ts};
    }
};

} // namespace cmf
