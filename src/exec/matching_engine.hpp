#pragma once

#include "book/order_book.hpp"
#include "common/BasicTypes.hpp"
#include "core/event.hpp"
#include "exec/fill.hpp"

#include <cstddef>
#include <vector>

namespace cmf
{

// Trade-based matching with FIFO queue-position simulation for at most one
// resting limit per side. On place(), the visible amount sitting at our price
// level in the current book snapshot is recorded as `queue_ahead` — orders
// already queued in front of us. Incoming opposite-direction trades drain
// `queue_ahead` first, and any remaining trade quantity fills us (partial
// fills allowed). Re-placing at the same side+price preserves queue_ahead;
// changing the price resets it from the latest book snapshot.
//
// Fills are at our limit price (passive maker). No latency, no fees.
class MatchingEngine
{
  public:
    struct Order
    {
        Side side{Side::Buy};
        Price price{0.0};
        Quantity remaining{0.0};
        Quantity queue_ahead{0.0};
        NanoTime placed_ts{0};
        bool active{false};
    };

    MatchingEngine() = default;

    // Bind the live book used to snapshot queue_ahead at place() time. The
    // pointer must outlive subsequent place() calls. May be null; if null,
    // queue_ahead defaults to 0 (joining-an-empty-level approximation).
    void set_book(const OrderBook* book) noexcept { book_ = book; }

    // Replace the resting order on `side` with a new limit at price/amount.
    // amount <= 0 cancels. If side+price match the current resting order,
    // queue_ahead is preserved and `remaining` is updated; otherwise
    // queue_ahead is re-snapshotted from the bound book.
    void place(Side side, Price price, Quantity amount, NanoTime ts);
    void cancel(Side side);

    // Apply a market trade. Any fills of our orders are appended to `out`.
    void on_trade(const Trade& trade, std::vector<Fill>& out);

    const Order& bid() const noexcept { return bid_; }
    const Order& ask() const noexcept { return ask_; }
    std::size_t fills_emitted() const noexcept { return fills_emitted_; }

  private:
    // Look up visible amount at `price` on `side` in the bound book; 0 if not
    // found or no book bound. Used to seed queue_ahead on a fresh place().
    Quantity visible_at(Side side, Price price) const;

    Order bid_{Side::Buy, 0.0, 0.0, 0.0, 0, false};
    Order ask_{Side::Sell, 0.0, 0.0, 0.0, 0, false};
    const OrderBook* book_{nullptr};
    std::size_t fills_emitted_{0};
};

} // namespace cmf
