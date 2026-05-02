#include "analytics/pnl_tracker.hpp"

namespace cmf {

void PnLTracker::apply_fill(const Fill& fill) {
    const double notional = fill.price * fill.amount;
    if (fill.side == Side::Buy) {
        position_ += fill.amount;
        cash_     -= notional;
    } else {
        position_ -= fill.amount;
        cash_     += notional;
    }
    ++fill_count_;
    volume_ += fill.amount;
    last_ts_ = fill.ts;
}

void PnLTracker::mark_to_market(NanoTime ts, Price mid) {
    last_ts_      = ts;
    last_mid_     = mid;
    last_equity_  = cash_ + position_ * mid;

    if (!seen_equity_) {
        max_equity_  = last_equity_;
        min_equity_  = last_equity_;
        peak_equity_ = last_equity_;
        seen_equity_ = true;
        return;
    }
    if (last_equity_ > max_equity_)  max_equity_  = last_equity_;
    if (last_equity_ < min_equity_)  min_equity_  = last_equity_;
    if (last_equity_ > peak_equity_) peak_equity_ = last_equity_;

    const double dd = peak_equity_ - last_equity_;
    if (dd > max_dd_) max_dd_ = dd;
}

}  // namespace cmf
