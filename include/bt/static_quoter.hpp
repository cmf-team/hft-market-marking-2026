#pragma once

#include "bt/order.hpp"
#include "bt/order_book.hpp"
#include "bt/strategy.hpp"
#include "bt/types.hpp"

#include <deque>

namespace bt {

// A trivial reference strategy that posts a passive buy one tick below the
// best bid and a passive sell one tick above the best ask. Refreshes on
// every snapshot: if the desired price has moved, cancel the old resting
// order and post a fresh one. Fixed quote size; no inventory management.
//
// The point of this strategy is *not* to make money — it's to exercise the
// full event loop end-to-end (book → quote → resting → fill → portfolio →
// stats) on real data, and to be small enough that any wiring bug shows up
// in its run.
//
// State machine per side:
//   - resting_id == 0 && !pending  → no order, will submit next on_book
//   - resting_id == 0 &&  pending  → submit in flight, awaiting on_submitted
//   - resting_id != 0 && !pending  → order resting, may need cancel-then-submit
//                                    if the desired price has moved
//
// Side ownership for async acks: a per-side `pending_*` flag is enough on
// its own because each on_submitted/on_reject pops the front of `pending_`
// in FIFO order — the latency layer guarantees that the n-th submit
// produces the n-th ack on the same channel.
class StaticQuoter final : public IStrategy {
public:
    explicit StaticQuoter(Qty quote_size) noexcept : quote_size_(quote_size) {}

    // ----- IStrategy ----------------------------------------------------------
    void on_book(const OrderBook& book, Timestamp now) override;
    void on_trade(const Trade&) override {}

    // ----- IFillSink ----------------------------------------------------------
    void on_submitted(OrderId id) override;
    void on_fill(const Fill& fill) override;
    void on_reject(const OrderReject& reject) override;
    void on_cancel_ack(OrderId id) override;
    void on_cancel_reject(const CancelReject& reject) override;

    // ----- Inspection (for tests) ---------------------------------------------
    [[nodiscard]] OrderId resting_buy_id()  const noexcept { return buy_.resting_id; }
    [[nodiscard]] OrderId resting_sell_id() const noexcept { return sell_.resting_id; }
    [[nodiscard]] Price   intended_buy_px()  const noexcept { return buy_.intended_px; }
    [[nodiscard]] Price   intended_sell_px() const noexcept { return sell_.intended_px; }

private:
    struct QuoteState {
        OrderId resting_id  = 0;   // id of the order currently resting (0 = none)
        Price   intended_px = 0;   // last price we tried to quote at (0 = no intent)
        bool    pending     = false; // submit in flight, awaiting on_submitted/on_reject
    };

    void refresh_side_(Side side, Price desired, Timestamp now);

    Qty                  quote_size_;
    QuoteState           buy_;
    QuoteState           sell_;
    // FIFO of submit attempts; on_submitted / on_reject pop the front to
    // identify which side the ack belongs to. Latency-layer FIFO ordering
    // makes this association unambiguous.
    std::deque<Side>     pending_acks_;
};

}  // namespace bt
