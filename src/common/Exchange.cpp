#include "common/Exchange.hpp"

#include <algorithm>
#include <vector>

namespace cmf
{

void Exchange::onLobUpdate(const LOBSnapshot& lob)
{
    current_lob_ = lob;
    current_time_ = lob.timestamp;
    has_data_ = true;
    matchOrders();
}

void Exchange::onTrade(const Trade& trade)
{
    current_time_ = trade.timestamp;
    matchAgainstTrade(trade);
}

OrderId Exchange::placeOrder(Side side, OrderType type, Price price, Quantity quantity)
{
    OrderId id = next_order_id_++;

    Order order;
    order.id = id;
    order.side = side;
    order.type = type;
    order.price = price;
    order.quantity = quantity;
    order.original_quantity = quantity;
    order.placed_at = current_time_;

    if (has_data_)
    {
        executeAgainstBook(order);
        if (order.quantity > 1e-12 && type == OrderType::Limit)
            active_orders_[id] = order;
    }
    else if (type == OrderType::Limit)
    {
        active_orders_[id] = order;
    }
    return id;
}

bool Exchange::cancelOrder(OrderId order_id)
{
    return active_orders_.erase(order_id) > 0;
}

void Exchange::cancelAll()
{
    active_orders_.clear();
}

std::vector<Fill> Exchange::pollFills()
{
    std::vector<Fill> out;
    out.swap(pending_fills_);
    return out;
}

Price Exchange::bestBid() const
{
    return has_data_ ? current_lob_.bids[0].price : 0.0;
}

Price Exchange::bestAsk() const
{
    return has_data_ ? current_lob_.asks[0].price : 0.0;
}

Price Exchange::midPrice() const
{
    return (bestBid() + bestAsk()) / 2.0;
}

Price Exchange::spread() const
{
    return bestAsk() - bestBid();
}

Quantity Exchange::executeAgainstBook(Order& order)
{
    if (!has_data_)
        return 0.0;

    const auto& levels = (order.side == Side::Buy) ? current_lob_.asks : current_lob_.bids;

    Quantity remaining = order.quantity;
    Quantity filled_total = 0.0;

    if (!partial_fills_)
    {
        // All-or-nothing on top level.
        const auto& top = levels[0];
        if (top.price <= 0 || top.amount < remaining)
            return 0.0;

        if (order.type == OrderType::Limit)
        {
            if (order.side == Side::Buy && top.price > order.price)
                return 0.0;
            if (order.side == Side::Sell && top.price < order.price)
                return 0.0;
        }
        Price fill_price = (order.type == OrderType::Limit) ? order.price : top.price;
        pending_fills_.push_back({order.id, order.side, fill_price, remaining, current_time_});
        order.quantity = 0;
        return remaining;
    }

    for (const auto& lvl : levels)
    {
        if (remaining <= 1e-12)
            break;
        if (lvl.price <= 0 || lvl.amount <= 0)
            break;

        if (order.type == OrderType::Limit)
        {
            if (order.side == Side::Buy && lvl.price > order.price)
                break;
            if (order.side == Side::Sell && lvl.price < order.price)
                break;
        }

        Quantity take = std::min(remaining, lvl.amount);
        Price fill_price = (order.type == OrderType::Limit) ? order.price : lvl.price;
        pending_fills_.push_back({order.id, order.side, fill_price, take, current_time_});
        remaining -= take;
        filled_total += take;
    }

    order.quantity = remaining;
    return filled_total;
}

void Exchange::matchAgainstTrade(const Trade& trade)
{
    if (active_orders_.empty())
        return;

    Side maker_side = (trade.side == Side::Sell) ? Side::Buy : Side::Sell;

    std::vector<std::pair<Price, OrderId>> eligible;
    for (auto& [id, o] : active_orders_)
    {
        if (o.side != maker_side || o.type != OrderType::Limit)
            continue;
        if (maker_side == Side::Buy && o.price < trade.price)
            continue;
        if (maker_side == Side::Sell && o.price > trade.price)
            continue;
        eligible.emplace_back(o.price, id);
    }
    if (eligible.empty())
        return;

    // Price priority: best (most aggressive) maker fills first.
    if (maker_side == Side::Buy)
        std::sort(eligible.begin(), eligible.end(),
                  [](const auto& a, const auto& b)
                  { return a.first > b.first; });
    else
        std::sort(eligible.begin(), eligible.end());

    Quantity remaining = trade.amount;
    std::vector<OrderId> done;

    for (auto& [_unused, id] : eligible)
    {
        if (remaining <= 1e-12)
            break;
        auto& order = active_orders_[id];

        if (!partial_fills_ && remaining < order.quantity)
            continue;

        Quantity take = std::min(remaining, order.quantity);
        pending_fills_.push_back({id, order.side, order.price, take, current_time_});
        order.quantity -= take;
        remaining -= take;
        if (order.quantity <= 1e-12)
            done.push_back(id);
    }
    for (OrderId id : done)
        active_orders_.erase(id);
}

void Exchange::matchOrders()
{
    if (!has_data_)
        return;

    std::vector<OrderId> done;
    for (auto& [id, order] : active_orders_)
    {
        executeAgainstBook(order);
        if (order.quantity <= 1e-12)
            done.push_back(id);
    }
    for (OrderId id : done)
        active_orders_.erase(id);
}

} // namespace cmf
