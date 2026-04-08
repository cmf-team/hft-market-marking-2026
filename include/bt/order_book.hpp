#pragma once

#include "bt/events.hpp"
#include "bt/types.hpp"

#include <cstddef>

namespace bt {

// In-memory representation of the latest 25-level snapshot. Holds no resting
// strategy orders — those live in the matcher. The OrderBook is the read
// surface that the engine, queue model, and matcher all consult.
//
// `apply(snap)` is a full overwrite (matches the snapshot semantics of the
// user's data; no incremental updates in v1).
class OrderBook {
public:
    OrderBook() = default;

    // Replace the entire book with the given snapshot.
    void apply(const BookSnapshot& snap) noexcept;

    // True until the first `apply()` call.
    [[nodiscard]] bool empty() const noexcept { return !populated_; }

    // Top-of-book accessors. Return 0 on an empty book.
    [[nodiscard]] Price best_bid() const noexcept;
    [[nodiscard]] Price best_ask() const noexcept;

    // Integer mid-price in ticks. Floor of (best_bid + best_ask) / 2 — loses
    // half a tick of precision on odd spreads, which is acceptable in an
    // integer-tick world (use the trade stream for true execution prices).
    // Returns 0 on an empty book.
    [[nodiscard]] Price mid() const noexcept;

    // Read the i-th level from the top of the given side. Caller must ensure
    // depth < kMaxLevels (debug-asserted).
    [[nodiscard]] const PriceLevel& level(Side side, std::size_t depth) const noexcept;

    // Returns the resting volume at exactly `price` on the given side, or 0
    // if that price is not currently one of the 25 levels. This is the exact
    // accessor the queue model needs in `on_submit` (initial queue_ahead) and
    // `on_snapshot` (volume delta).
    [[nodiscard]] Qty volume_at(Side side, Price price) const noexcept;

    // Timestamp of the most recently applied snapshot, 0 before any apply.
    [[nodiscard]] Timestamp last_update_ts() const noexcept { return snap_.ts; }

    // Direct access to the underlying snapshot (e.g. for the queue model's
    // prev/curr diff).
    [[nodiscard]] const BookSnapshot& snapshot() const noexcept { return snap_; }

private:
    BookSnapshot snap_{};
    bool         populated_ = false;
};

}  // namespace bt
