#include "bt/avellaneda_stoikov_quoter.hpp"

#include "bt/exchange.hpp"

#include <algorithm>
#include <cmath>

namespace bt {

AvellanedaStoikovQuoter::AvellanedaStoikovQuoter(const Params& p) noexcept
    : params_(p),
      sigma2_(p.sigma_init * p.sigma_init) {}

void AvellanedaStoikovQuoter::update_volatility_(Price mid) noexcept {
    if (!have_last_mid_) {
        last_mid_      = mid;
        have_last_mid_ = true;
        return;
    }
    const double r  = static_cast<double>(mid - last_mid_);
    const double r2 = r * r;
    const double a  = params_.vol_ewma_alpha;
    if (a > 0.0) {
        sigma2_ = (1.0 - a) * sigma2_ + a * r2;
    }
    last_mid_ = mid;
}

void AvellanedaStoikovQuoter::on_book(const OrderBook& book, Timestamp now) {
    if (book.empty() || exchange_ == nullptr) return;

    const Price s_ticks = book.mid();
    if (s_ticks == 0) return;
    const Price best_bid = book.best_bid();
    const Price best_ask = book.best_ask();
    if (best_bid == 0 || best_ask == 0) return;

    if (!have_session_) {
        session_start_ = now;
        have_session_  = true;
    }
    update_volatility_(s_ticks);

    // Normalized time-to-horizon. Clamped to a small epsilon so the spread
    // and skew stay finite as we approach the end of the session.
    const double elapsed = static_cast<double>(now - session_start_);
    const double horizon = static_cast<double>(params_.horizon_us);
    double       t_frac  = (horizon > 0.0) ? (horizon - elapsed) / horizon : 1.0;
    t_frac               = std::clamp(t_frac, 1e-3, 1.0);

    const double gamma = params_.gamma;
    const double k     = std::max(1e-9, params_.k);
    const double q     = static_cast<double>(inventory_);
    const double s     = static_cast<double>(s_ticks);

    // Reservation price: skew the mid by inventory * risk * variance * time.
    const double r = s - q * gamma * sigma2_ * t_frac;

    // Avellaneda-Stoikov optimal spread. The first term widens the spread
    // when uncertainty (sigma^2 * (T-t)) is high; the second is the
    // adverse-selection premium tied to order arrival intensity k.
    const double spread = gamma * sigma2_ * t_frac
                        + (2.0 / gamma) * std::log(1.0 + gamma / k);
    const double half = 0.5 * spread;

    // Round outward so the quoted spread is at least as wide as the model's.
    const Price desired_bid = static_cast<Price>(std::floor(r - half));
    const Price desired_ask = static_cast<Price>(std::ceil (r + half));

    // Post-only safety: a quote on or through the touch would be rejected by
    // the matcher's would-cross check. Pull each side at least one tick away
    // from the opposite touch.
    const Price clamped_bid = std::min(desired_bid, best_ask - 1);
    const Price clamped_ask = std::max(desired_ask, best_bid + 1);

    refresh_side_(Side::Buy,  clamped_bid, now);
    refresh_side_(Side::Sell, clamped_ask, now);
}

void AvellanedaStoikovQuoter::refresh_side_(Side side, Price desired, Timestamp now) {
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

void AvellanedaStoikovQuoter::on_submitted(OrderId id) {
    if (pending_acks_.empty()) return;
    const Side s = pending_acks_.front();
    pending_acks_.pop_front();
    QuoteState& q = (s == Side::Buy) ? buy_ : sell_;
    q.resting_id = id;
    q.pending    = false;
}

void AvellanedaStoikovQuoter::on_fill(const Fill& fill) {
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

void AvellanedaStoikovQuoter::on_reject(const OrderReject& /*reject*/) {
    if (pending_acks_.empty()) return;
    const Side s = pending_acks_.front();
    pending_acks_.pop_front();
    QuoteState& q = (s == Side::Buy) ? buy_ : sell_;
    q.pending     = false;
    q.intended_px = 0;
}

void AvellanedaStoikovQuoter::on_cancel_ack(OrderId /*id*/) {}
void AvellanedaStoikovQuoter::on_cancel_reject(const CancelReject& /*reject*/) {}

}  // namespace bt
