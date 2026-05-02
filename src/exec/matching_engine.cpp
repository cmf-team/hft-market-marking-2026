#include "exec/matching_engine.hpp"

#include <algorithm>

namespace cmf
{

Quantity MatchingEngine::visible_at(Side side, Price price) const
{
    if (!book_ || book_->empty())
        return 0.0;
    for (std::size_t i = 0; i < OrderBook::kDepth; ++i)
    {
        const PriceLevel lvl = (side == Side::Buy) ? book_->bid_at(i) : book_->ask_at(i);
        if (lvl.amount <= 0.0)
            continue;
        if (lvl.price == price)
            return lvl.amount;
    }
    return 0.0;
}

void MatchingEngine::place(Side side, Price price, Quantity amount, NanoTime ts)
{
    if (amount <= 0.0)
    {
        cancel(side);
        return;
    }
    Order& o = (side == Side::Buy) ? bid_ : ask_;

    // Same side+price as the live order: keep queue position, just resize.
    if (o.active && o.price == price)
    {
        o.remaining = amount;
        o.placed_ts = ts;
        return;
    }

    o.side = side;
    o.price = price;
    o.remaining = amount;
    o.queue_ahead = visible_at(side, price);
    o.placed_ts = ts;
    o.active = true;
}

void MatchingEngine::cancel(Side side)
{
    Order& o = (side == Side::Buy) ? bid_ : ask_;
    o.active = false;
    o.remaining = 0.0;
    o.queue_ahead = 0.0;
}

void MatchingEngine::on_trade(const Trade& trade, std::vector<Fill>& out)
{
    // Market sell hits resting bids; market buy hits resting asks.
    Order& target = (trade.side == Side::Sell) ? bid_ : ask_;
    if (!target.active || target.remaining <= 0.0)
        return;

    const bool crosses = (target.side == Side::Buy)
                             ? (trade.price <= target.price)
                             : (trade.price >= target.price);
    if (!crosses)
        return;

    // Drain queue ahead of us first. A trade printing strictly through our
    // price means the aggressor has already cleared everything at our level
    // ahead of us, so the same min(trade, queue_ahead) accounting still holds:
    // any remainder reaches us.
    Quantity remaining_trade = trade.amount;
    if (target.queue_ahead > 0.0)
    {
        const Quantity consumed = std::min(remaining_trade, target.queue_ahead);
        target.queue_ahead -= consumed;
        remaining_trade -= consumed;
        if (remaining_trade <= 0.0)
            return;
    }

    const Quantity qty = std::min(target.remaining, remaining_trade);
    if (qty <= 0.0)
        return;

    Fill f;
    f.ts = trade.ts;
    f.side = target.side;
    f.price = target.price;
    f.amount = qty;
    out.push_back(f);
    ++fills_emitted_;

    target.remaining -= qty;
    if (target.remaining <= 0.0)
    {
        target.active = false;
        target.remaining = 0.0;
        target.queue_ahead = 0.0;
    }
}

} // namespace cmf
