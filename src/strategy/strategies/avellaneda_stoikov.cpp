#include "strategy/strategies/avellaneda_stoikov.hpp"

#include "book/order_book.hpp"

#include <algorithm>
#include <cmath>

namespace cmf
{

AvellanedaStoikov::AvellanedaStoikov(Params params)
    : params_(params), vol_(params.vol_window, params.vol_dt) {}

void AvellanedaStoikov::on_fill(const Fill& fill)
{
    if (fill.side == Side::Buy)
        inventory_ += fill.amount;
    else
        inventory_ -= fill.amount;
}

void AvellanedaStoikov::on_book_update(const OrderBook& book)
{
    if (book.empty())
        return;

    const PriceLevel bb = book.best_bid();
    const PriceLevel ba = book.best_ask();
    const Price s = 0.5 * (bb.price + ba.price);
    const NanoTime ts = book.timestamp();

    if (have_prev_mid_)
    {
        vol_.update(prev_mid_, s);
    }
    prev_mid_ = s;
    have_prev_mid_ = true;

    const double sigma = vol_.sigma();

    double time_remaining = static_cast<double>(session_end_ - ts);
    if (time_remaining <= 0.0)
        time_remaining = 1e-9;

    // Clamp the exp argument to keep `factor` finite even if vol_dt / session_end
    // are mis-scaled. Without this, sigma^2 * (T-t) can overflow exp() and poison
    // every downstream computation with inf/NaN.xs
    constexpr double kMaxExpArg = 50.0;
    double exp_arg = sigma * sigma * time_remaining;
    if (exp_arg > kMaxExpArg)
        exp_arg = kMaxExpArg;

    const double s2 = s * s;
    const double raw_factor = params_.gamma * s2 * (std::exp(exp_arg) - 1.0);

    // Floor the half-spread at min_half_spread_ticks · tick. Sub-tick AS output
    // produces quotes that straddle mid by adjacent ticks and chronically
    // cross the book on the next update.
    const Price tick = params_.tick_size;
    const double min_half = params_.min_half_spread_ticks * tick;
    const double half_spread = std::max(0.5 * raw_factor, min_half);
    const double factor = 2.0 * half_spread;

    // Diagnostics: midpoint between R^a and R^b is r = s - q*factor; spread = factor.
    reservation_ = s - inventory_ * factor;
    half_spread_ = half_spread;

    const double raw_ask = s + 0.5 * (1.0 - 2.0 * inventory_) * factor;
    const double raw_bid = s + 0.5 * (-1.0 - 2.0 * inventory_) * factor;
    Price p_bid = std::floor(raw_bid / tick) * tick;
    Price p_ask = std::ceil(raw_ask / tick) * tick;

    if (!std::isfinite(p_bid) || !std::isfinite(p_ask))
    {
        // Quote math is degenerate; cancel any resting orders and skip this update.
        bid_active_ = false;
        ask_active_ = false;
        if (me_)
        {
            me_->place(Side::Buy, 0.0, 0.0, ts);
            me_->place(Side::Sell, 0.0, 0.0, ts);
        }
        return;
    }

    if (p_bid <= 0.0)
        p_bid = tick;
    if (p_ask <= p_bid)
        p_ask = p_bid + tick;

    bid_price_ = p_bid;
    ask_price_ = p_ask;

    // Dynamic quote sizing: clamp each leg so a full fill cannot breach the
    // inventory cap. A static `quote_size` allows e.g. a 10k buy near q_max
    // to push inventory past q_max — the boolean `<` / `>` guard fires too
    // late. Sizing the leg to the available headroom prevents this.
    const Quantity buy_room = std::max<Quantity>(params_.q_max - inventory_, 0.0);
    const Quantity sell_room = std::max<Quantity>(inventory_ - params_.q_min, 0.0);
    const Quantity bid_qty = std::min(params_.quote_size, buy_room);
    const Quantity ask_qty = std::min(params_.quote_size, sell_room);

    bool post_bid = bid_qty > 0.0;
    bool post_ask = ask_qty > 0.0;

    // No-cross guard: a passive maker quote must not be marketable. If our
    // bid would lift the offer (>= best_ask) or our ask would hit the bid
    // (<= best_bid), cancel that leg for this update — the AS reservation
    // skew has pushed our quote across the book and any fill there would
    // be a guaranteed loss at the wrong side of the spread.
    /*if (post_bid && p_bid >= ba.price) {
        post_bid = false;
        ++cross_skips_;
    }
    if (post_ask && p_ask <= bb.price) {
        post_ask = false;
        ++cross_skips_;
    } */

    bid_active_ = post_bid;
    ask_active_ = post_ask;
    ++requote_count_;

    if (me_)
    {
        me_->place(Side::Buy, p_bid, post_bid ? bid_qty : 0.0, ts);
        me_->place(Side::Sell, p_ask, post_ask ? ask_qty : 0.0, ts);
    }
}

} // namespace cmf
