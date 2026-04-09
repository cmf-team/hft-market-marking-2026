#include "bt/portfolio.hpp"

#include <cstdlib>

namespace bt {

namespace {

// Sign of a signed Qty: -1, 0, +1.
constexpr int sign(Qty q) noexcept {
    return (q > 0) - (q < 0);
}

}  // namespace

void Portfolio::on_fill(const Fill& fill) noexcept {
    // Signed delta: a Buy fill increases the position, a Sell decreases it.
    const Qty delta = (fill.side == Side::Buy) ? fill.qty : -fill.qty;

    if (position_ == 0) {
        // Opening from flat — no PnL to realize, just establish the position.
        position_  = delta;
        avg_entry_ = fill.price;
        return;
    }

    if (sign(position_) == sign(delta)) {
        // Same direction — add to the position and roll the weighted average
        // forward. Integer division loses at most 1 tick of precision per
        // partial add; for an HFT-tick-grid backtest that's well below the
        // numerical noise floor we care about.
        const Qty   abs_pos   = std::llabs(position_);
        const Qty   abs_delta = std::llabs(delta);
        const Qty   new_abs   = abs_pos + abs_delta;
        avg_entry_ = (avg_entry_ * abs_pos + fill.price * abs_delta) / new_abs;
        position_ += delta;
        return;
    }

    // Opposite direction — closes (or flips) the position. Compute the
    // closed quantity, realize PnL on it at the OLD avg_entry, then either
    // reduce the position, flatten it, or flip into a new fresh position.
    const Qty abs_pos      = std::llabs(position_);
    const Qty abs_delta    = std::llabs(delta);
    const Qty closed_qty   = (abs_delta < abs_pos) ? abs_delta : abs_pos;

    // Long  → realized = (fill_price - avg_entry) * closed
    // Short → realized = (avg_entry - fill_price) * closed
    // Both collapse to: (fill_price - avg_entry) * closed * sign(position_)
    realized_ += static_cast<std::int64_t>(fill.price - avg_entry_)
               * static_cast<std::int64_t>(closed_qty)
               * static_cast<std::int64_t>(sign(position_));

    if (abs_delta < abs_pos) {
        // Partial close — position shrinks, avg_entry stays put (the
        // remaining open lots were originally established at avg_entry).
        position_ += delta;
    } else if (abs_delta == abs_pos) {
        // Full close — back to flat.
        position_  = 0;
        avg_entry_ = 0;
    } else {
        // Flip — close the entire old position then open the remainder
        // in the opposite direction at fill.price.
        const Qty remainder = abs_delta - abs_pos;
        position_  = (delta > 0) ? remainder : -remainder;
        avg_entry_ = fill.price;
    }
}

void Portfolio::mark_to_market(Price mid) noexcept {
    if (position_ == 0) {
        unrealized_ = 0;
        return;
    }
    // (mid - avg_entry) * position_ — works for both directions because
    // position_ carries the sign.
    unrealized_ = static_cast<std::int64_t>(mid - avg_entry_)
                * static_cast<std::int64_t>(position_);
}

}  // namespace bt
