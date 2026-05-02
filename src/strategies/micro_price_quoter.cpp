#include "bt/micro_price_quoter.hpp"

#include "bt/events.hpp"
#include "bt/exchange.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace bt {

MicroPriceQuoter::MicroPriceQuoter(const Params& p) noexcept : params_(p) {
    if (params_.imbalance_depth == 0) params_.imbalance_depth = 1;
    if (params_.imbalance_depth > kMaxLevels) params_.imbalance_depth = kMaxLevels;
}

double MicroPriceQuoter::compute_micro_price_(const OrderBook& book) const noexcept {
    // Compute per-side VWAP across the first `imbalance_depth` levels and
    // total size on each side. At depth = 1 the VWAPs collapse to
    // best_bid / best_ask exactly (the paper's level-1 micro-price).
    //
    // Mixing depth-N sizes with level-1 prices (the previous behavior) was
    // a unit error — the M* formula assumes (P, Q) come from the same
    // level. Aggregating both via VWAP keeps the formula consistent at
    // any depth.
    std::int64_t wb_num = 0;   // Σ price_i * qty_i  (bid side)
    std::int64_t wa_num = 0;   // Σ price_i * qty_i  (ask side)
    Qty          qb     = 0;
    Qty          qa     = 0;
    for (std::size_t i = 0; i < params_.imbalance_depth; ++i) {
        const auto& bl = book.level(Side::Buy,  i);
        const auto& al = book.level(Side::Sell, i);
        if (bl.amount > 0) {
            wb_num += static_cast<std::int64_t>(bl.price) *
                      static_cast<std::int64_t>(bl.amount);
            qb     += bl.amount;
        }
        if (al.amount > 0) {
            wa_num += static_cast<std::int64_t>(al.price) *
                      static_cast<std::int64_t>(al.amount);
            qa     += al.amount;
        }
    }
    if (qa <= 0 || qb <= 0) {
        // One side empty → micro-price degenerate; fall back to mid.
        return static_cast<double>(book.mid());
    }
    const double pb = static_cast<double>(wb_num) / static_cast<double>(qb);
    const double pa = static_cast<double>(wa_num) / static_cast<double>(qa);
    const double denom = static_cast<double>(qa + qb);
    // M* = (qa * pb + qb * pa) / (qa + qb) — cross-weighting: heavier side
    // pulls M* toward the OTHER touch.
    return (static_cast<double>(qa) * pb + static_cast<double>(qb) * pa) / denom;
}

void MicroPriceQuoter::on_book(const OrderBook& book, Timestamp now) {
    if (book.empty() || exchange_ == nullptr) return;

    const Price best_bid = book.best_bid();
    const Price best_ask = book.best_ask();
    if (best_bid == 0 || best_ask == 0) return;

    last_micro_ = compute_micro_price_(book);

    const double skew_offset = params_.inventory_skew *
                               static_cast<double>(inventory_);
    const double mp_shifted  = last_micro_ - skew_offset;
    const double half        = static_cast<double>(params_.half_spread);

    // Round outward so the quoted spread is at least 2 * half_spread wide.
    const Price desired_bid = static_cast<Price>(std::floor(mp_shifted - half));
    const Price desired_ask = static_cast<Price>(std::ceil (mp_shifted + half));

    Price clamped_bid;
    Price clamped_ask;
    if (params_.passive_only) {
        // Never sit inside the spread: cap at the same-side touch. A bid
        // above best_bid joins the queue; a bid below stays where it is.
        // This is the pure market-making posture — fewer fills, less
        // adverse selection.
        clamped_bid = std::min(desired_bid, best_bid);
        clamped_ask = std::max(desired_ask, best_ask);
    } else {
        // Aggressive mode: only enforce the post-only would-cross check.
        // Quotes can sit anywhere strictly inside the spread.
        clamped_bid = std::min(desired_bid, best_ask - 1);
        clamped_ask = std::max(desired_ask, best_bid + 1);
    }

    refresh_side_(Side::Buy,  clamped_bid, now);
    refresh_side_(Side::Sell, clamped_ask, now);
}

void MicroPriceQuoter::refresh_side_(Side side, Price desired, Timestamp now) {
    QuoteState& q = (side == Side::Buy) ? buy_ : sell_;

    if (q.pending) return;
    if (q.resting_id != 0 && q.intended_px == desired) return;

    if (q.resting_id != 0) {
        exchange_->cancel(now, q.resting_id);
        q.resting_id = 0;
    }
    exchange_->post_only_limit(now, side, desired, params_.quote_size);
    q.pending     = true;
    q.intended_px = desired;
    pending_acks_.push_back(side);
}

void MicroPriceQuoter::on_submitted(OrderId id) {
    if (pending_acks_.empty()) return;
    const Side s = pending_acks_.front();
    pending_acks_.pop_front();
    QuoteState& q = (s == Side::Buy) ? buy_ : sell_;
    q.resting_id = id;
    q.pending    = false;
}

void MicroPriceQuoter::on_fill(const Fill& fill) {
    if (fill.side == Side::Buy) {
        inventory_ += fill.qty;
        if (fill.id == buy_.resting_id) {
            buy_.resting_id  = 0;
            buy_.intended_px = 0;
        }
    } else {
        inventory_ -= fill.qty;
        if (fill.id == sell_.resting_id) {
            sell_.resting_id  = 0;
            sell_.intended_px = 0;
        }
    }
}

void MicroPriceQuoter::on_reject(const OrderReject& /*reject*/) {
    if (pending_acks_.empty()) return;
    const Side s = pending_acks_.front();
    pending_acks_.pop_front();
    QuoteState& q = (s == Side::Buy) ? buy_ : sell_;
    q.pending     = false;
    q.intended_px = 0;
}

void MicroPriceQuoter::on_cancel_ack(OrderId /*id*/) {}
void MicroPriceQuoter::on_cancel_reject(const CancelReject& /*reject*/) {}

}  // namespace bt
