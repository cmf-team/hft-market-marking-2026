#pragma once

#include "bt/order.hpp"
#include "bt/order_book.hpp"
#include "bt/strategy.hpp"
#include "bt/types.hpp"

#include <deque>

namespace bt {

// Micro-price market-making strategy ("The Micro-Price: A High-Frequency
// Estimator of Future Prices", Stoikov, 2018).
//
// The micro-price is a size-weighted mid that biases toward the side with
// LESS depth — the intuition is that the heavier side is "holding the
// price" and the lighter side is more likely to be consumed next. For the
// best-bid/ask, the closed-form weighting is
//
//   M* = (Q_a * P_b + Q_b * P_a) / (Q_a + Q_b)
//
// equivalently
//
//   M* = mid + (I - 1/2) * spread,   where  I = Q_b / (Q_b + Q_a).
//
// Stoikov's full paper extends this to a Markov-chain estimator G_k(I, S)
// fit from data; the level-1 weighted mid above is the zeroth-order term
// and is what this strategy uses directly.
//
// Quotes are placed as
//
//   bid = floor(M* - half_spread - skew * inventory)
//   ask = ceil (M* + half_spread - skew * inventory)
//
// where `half_spread` is a configured passive distance (in ticks) and
// `skew` (ticks per inventory unit) shifts both quotes against the
// position so the quoter naturally leans toward flat. Both quotes are
// post-only and clamped at least one tick outside the touch.
//
// Optional `imbalance_depth` widens I to sum the first N levels per side;
// at depth=1 you get the paper's level-1 definition exactly. Higher depths
// are a practical extension that smooths flicker on thin top-of-book.
class MicroPriceQuoter final : public IStrategy {
public:
    struct Params {
        Qty         quote_size       = 1;      // size on each side
        Price       half_spread      = 1;      // ticks from M* to each quote
        double      inventory_skew   = 0.0;    // ticks per inventory unit
        std::size_t imbalance_depth  = 1;      // levels of book averaged into I

        // When true, quotes never sit inside the spread: a bid above the
        // best bid is clamped to the best bid (joins the queue) and an ask
        // below the best ask is clamped to the best ask. Pure market-making
        // posture — fewer fills, but each one is less adversely selected.
        // When false, the only constraint is the post-only would-cross
        // check; quotes can be placed inside the spread, which fills more
        // aggressively at the cost of significantly worse selection.
        bool        passive_only     = true;
    };

    explicit MicroPriceQuoter(const Params& p) noexcept;

    // ----- IStrategy ----------------------------------------------------------
    void on_book(const OrderBook& book, Timestamp now) override;
    void on_trade(const Trade&) override {}

    // ----- IFillSink ----------------------------------------------------------
    void on_submitted(OrderId id) override;
    void on_fill(const Fill& fill) override;
    void on_reject(const OrderReject& reject) override;
    void on_cancel_ack(OrderId id) override;
    void on_cancel_reject(const CancelReject& reject) override;

    // ----- Inspection ---------------------------------------------------------
    [[nodiscard]] OrderId resting_buy_id()  const noexcept { return buy_.resting_id; }
    [[nodiscard]] OrderId resting_sell_id() const noexcept { return sell_.resting_id; }
    [[nodiscard]] Price   intended_buy_px()  const noexcept { return buy_.intended_px; }
    [[nodiscard]] Price   intended_sell_px() const noexcept { return sell_.intended_px; }
    [[nodiscard]] Qty     inventory()        const noexcept { return inventory_; }
    [[nodiscard]] double  last_micro_price() const noexcept { return last_micro_; }

private:
    struct QuoteState {
        OrderId resting_id  = 0;
        Price   intended_px = 0;
        bool    pending     = false;
    };

    [[nodiscard]] double compute_micro_price_(const OrderBook& book) const noexcept;
    void                 refresh_side_(Side side, Price desired, Timestamp now);

    Params           params_;
    QuoteState       buy_;
    QuoteState       sell_;
    std::deque<Side> pending_acks_;

    Qty    inventory_  = 0;       // signed; positive = long
    double last_micro_ = 0.0;     // most-recent micro-price, in ticks
};

}  // namespace bt
