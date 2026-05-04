#pragma once

#include "bt/order.hpp"
#include "bt/order_book.hpp"
#include "bt/strategy.hpp"
#include "bt/types.hpp"

#include <deque>

namespace bt {

// Avellaneda-Stoikov market-making strategy ("High-Frequency Trading in a
// Limit Order Book", Avellaneda & Stoikov, 2008).
//
// Continuously quotes a passive bid/ask pair around a reservation price that
// is skewed by current inventory. The optimal half-spread is derived from
// the trader's risk aversion (gamma), the variance of the mid-price (sigma^2),
// the remaining time to horizon (T - t), and the limit-order arrival
// intensity parameter (k):
//
//   r       = s - q * gamma * sigma^2 * (T - t)
//   spread  = gamma * sigma^2 * (T - t) + (2 / gamma) * ln(1 + gamma / k)
//   bid     = r - spread / 2
//   ask     = r + spread / 2
//
// Practical adaptations made for an integer-tick backtest:
//   - sigma^2 is estimated as an EWMA of squared mid-price increments
//     (in ticks). The configured `sigma_init` seeds it.
//   - (T - t) is normalized to the unit interval as `(horizon - elapsed) / horizon`,
//     with elapsed measured from the first on_book callback. Clamped to a
//     small epsilon near the horizon to keep quotes finite.
//   - Quotes are rounded outward to integer ticks (floor for the bid, ceil
//     for the ask) and then clamped to one tick outside the touch so the
//     post-only check at the matcher never fails on a stable book.
//   - Inventory is tracked from our own fills; a Buy fill increments q, a
//     Sell fill decrements it.
//
// The state machine for managing in-flight submits/cancels mirrors
// StaticQuoter: a per-side `pending` flag plus a FIFO of pending sides used
// to map on_submitted / on_reject acks back to the side they belong to. The
// latency layer guarantees FIFO ordering of submit acks, which makes that
// association unambiguous.
class AvellanedaStoikovQuoter final : public IStrategy {
public:
    struct Params {
        Qty       quote_size      = 1;       // size posted on each side
        double    gamma           = 0.1;     // risk aversion
        double    k               = 1.5;     // order arrival intensity
        double    sigma_init      = 1.0;     // seed for sigma (ticks per book step)
        double    vol_ewma_alpha  = 0.05;    // EWMA smoothing for sigma^2; 0 disables update
        Timestamp horizon_us      = 86'400'000'000LL;  // session length, default = 1 day
    };

    explicit AvellanedaStoikovQuoter(const Params& p) noexcept;

    // ----- IStrategy ----------------------------------------------------------
    void on_book(const OrderBook& book, Timestamp now) override;
    void on_trade(const Trade&) override {}

    // ----- IFillSink ----------------------------------------------------------
    void on_submitted(OrderId id) override;
    void on_fill(const Fill& fill) override;
    void on_reject(const OrderReject& reject) override;
    void on_cancel_ack(OrderId id) override;
    void on_cancel_reject(const CancelReject& reject) override;

    // ----- Inspection (for tests / reporting) ---------------------------------
    [[nodiscard]] OrderId resting_buy_id()  const noexcept { return buy_.resting_id; }
    [[nodiscard]] OrderId resting_sell_id() const noexcept { return sell_.resting_id; }
    [[nodiscard]] Price   intended_buy_px()  const noexcept { return buy_.intended_px; }
    [[nodiscard]] Price   intended_sell_px() const noexcept { return sell_.intended_px; }
    [[nodiscard]] Qty     inventory()       const noexcept { return inventory_; }
    [[nodiscard]] double  sigma2()          const noexcept { return sigma2_; }

private:
    struct QuoteState {
        OrderId resting_id  = 0;
        Price   intended_px = 0;
        bool    pending     = false;
    };

    void refresh_side_(Side side, Price desired, Timestamp now);
    void update_volatility_(Price mid) noexcept;

    Params           params_;
    QuoteState       buy_;
    QuoteState       sell_;
    std::deque<Side> pending_acks_;

    Qty       inventory_      = 0;     // signed; positive = long
    double    sigma2_         = 1.0;   // EWMA of squared mid increments (ticks^2)
    Price     last_mid_       = 0;
    bool      have_last_mid_  = false;
    Timestamp session_start_  = 0;
    bool      have_session_   = false;
};

}  // namespace bt
