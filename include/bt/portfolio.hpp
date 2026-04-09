#pragma once

#include "bt/order.hpp"
#include "bt/types.hpp"

#include <cstdint>

namespace bt {

// Tracks position, weighted-average entry price, and PnL for a single
// instrument. All math is done in integer ticks × qty — no doubles touch
// this class. The report writer is the only place that converts to a human
// price via InstrumentSpec::tick_size.
//
// Accounting model: weighted-average cost. When the same-direction fills
// add to the position, the avg entry is rolled forward. When opposite-side
// fills reduce the position, realized PnL is booked at the OLD avg entry
// vs the fill price for the closed quantity. A fill that flips the
// position (closes the existing side and opens the other side in one shot)
// is split into a close + an open: the close realizes PnL on the entire
// old position, and the remainder establishes a fresh position at the
// fill price.
//
// PnL units: int64 ticks × qty. Convert to a human currency value as
// `total_pnl_ticks() * tick_size * tick_value` at the report boundary.
class Portfolio {
public:
    Portfolio() = default;

    // Apply a fill to the position. The Fill::side field indicates the side
    // of the resting order being filled (Buy → we bought; Sell → we sold).
    void on_fill(const Fill& fill) noexcept;

    // Update unrealized PnL using the supplied mark price (in ticks). The
    // engine calls this on every book update. Called with mid=0 / no
    // position is a no-op.
    void mark_to_market(Price mid) noexcept;

    // ----- Inspection ---------------------------------------------------------

    // Signed position: positive when long, negative when short, 0 when flat.
    [[nodiscard]] Qty position() const noexcept { return position_; }

    // Average entry price of the currently open position, in ticks. Zero
    // when the position is flat. Mostly useful for the report.
    [[nodiscard]] Price avg_entry_price() const noexcept { return avg_entry_; }

    // Realized PnL in int64 ticks × qty. Strictly accumulating — never
    // touched by mark_to_market.
    [[nodiscard]] std::int64_t realized_pnl_ticks() const noexcept { return realized_; }

    // Unrealized PnL at the last mark, in int64 ticks × qty. Recomputed
    // each time mark_to_market is called.
    [[nodiscard]] std::int64_t unrealized_pnl_ticks() const noexcept { return unrealized_; }

    // Convenience sum. Note unrealized is only correct as of the last
    // mark_to_market call.
    [[nodiscard]] std::int64_t total_pnl_ticks() const noexcept {
        return realized_ + unrealized_;
    }

private:
    Qty          position_   = 0;   // signed
    Price        avg_entry_  = 0;   // valid iff position_ != 0
    std::int64_t realized_   = 0;   // ticks × qty
    std::int64_t unrealized_ = 0;   // ticks × qty, refreshed by mark_to_market
};

}  // namespace bt
