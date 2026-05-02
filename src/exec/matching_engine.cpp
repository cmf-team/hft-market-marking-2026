#include "exec/matching_engine.hpp"

#include <algorithm>

namespace cmf {

void MatchingEngine::place(Side side, Price price, Quantity amount, NanoTime ts) {
    if (amount <= 0.0) {
        cancel(side);
        return;
    }
    Order& o = (side == Side::Buy) ? bid_ : ask_;
    o.side      = side;
    o.price     = price;
    o.remaining = amount;
    o.placed_ts = ts;
    o.active    = true;
}

void MatchingEngine::cancel(Side side) {
    Order& o = (side == Side::Buy) ? bid_ : ask_;
    o.active    = false;
    o.remaining = 0.0;
}

void MatchingEngine::on_trade(const Trade& trade, std::vector<Fill>& out) {
    // Market sell hits resting bids; market buy hits resting asks.
    Order& target = (trade.side == Side::Sell) ? bid_ : ask_;
    if (!target.active || target.remaining <= 0.0) return;

    const bool crosses = (target.side == Side::Buy)
                         ? (trade.price <= target.price)
                         : (trade.price >= target.price);
    if (!crosses) return;

    const Quantity qty = std::min(target.remaining, trade.amount);
    if (qty <= 0.0) return;

    Fill f;
    f.ts     = trade.ts;
    f.side   = target.side;
    f.price  = target.price;
    f.amount = qty;
    out.push_back(f);
    ++fills_emitted_;

    target.remaining -= qty;
    if (target.remaining <= 0.0) {
        target.active    = false;
        target.remaining = 0.0;
    }
}

}  // namespace cmf
