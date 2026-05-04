#pragma once

#include <cmath>
#include <cstdint>

namespace bt {

// Microsecond-resolution event timestamps. Signed so deltas behave.
using Timestamp = std::int64_t;

// Prices are stored as integer ticks, NOT as doubles. A Price value of 110435
// with tick_size = 1e-7 represents 0.0110435. All comparisons, equality checks,
// hashing, and arithmetic on prices are exact. Doubles are confined to the I/O
// boundary (CSV parse, report write).
using Price = std::int64_t;

// Quantities are integer base units. The unit is per-instrument; the user's
// sample data uses whole-integer amounts so qty_scale = 1.
using Qty = std::int64_t;

// Strategy-side opaque order identifier.
using OrderId = std::uint64_t;

enum class Side : std::uint8_t {
    Buy,
    Sell,
};

// Only PostOnly exists in v1. The enum is here so adding GTC/IOC/FOK later is
// a one-line change in the matcher's submit path.
enum class TimeInForce : std::uint8_t {
    PostOnly,
};

enum class RejectReason : std::uint8_t {
    WouldCross,
};

// Per-instrument scale factors. The single source of truth for converting
// between human/double values at the I/O boundary and internal integer values.
struct InstrumentSpec {
    double tick_size;  // e.g. 1e-7 for the user's sample data
    double qty_scale;  // multiply double amount by this to get integer Qty; e.g. 1.0
};

// Convert a double price to integer ticks. Uses round-to-nearest so prices
// that are off by floating-point epsilon snap onto the grid. Callers that
// care whether the input was on-grid should call is_on_tick_grid first.
[[nodiscard]] inline Price to_ticks(double price, const InstrumentSpec& spec) noexcept {
    return static_cast<Price>(std::llround(price / spec.tick_size));
}

// Convert integer ticks back to a human-readable double. Use only at the
// reporting boundary; never compare doubles in engine code.
[[nodiscard]] inline double from_ticks(Price ticks, const InstrumentSpec& spec) noexcept {
    return static_cast<double>(ticks) * spec.tick_size;
}

// Returns true if `price` lies on the tick grid within the given tolerance
// (expressed as a fraction of one tick). The default 1e-6 means "within
// one millionth of a tick" — comfortably tighter than any realistic source
// of float noise from CSV parsing, but loose enough to ignore IEEE-754 epsilon.
[[nodiscard]] inline bool is_on_tick_grid(double price,
                                          const InstrumentSpec& spec,
                                          double tol = 1e-6) noexcept {
    const double in_ticks = price / spec.tick_size;
    const double rounded = std::round(in_ticks);
    return std::fabs(in_ticks - rounded) < tol;
}

// Convert a double quantity to internal integer units.
[[nodiscard]] inline Qty to_qty(double amount, const InstrumentSpec& spec) noexcept {
    return static_cast<Qty>(std::llround(amount * spec.qty_scale));
}

[[nodiscard]] inline double from_qty(Qty qty, const InstrumentSpec& spec) noexcept {
    return static_cast<double>(qty) / spec.qty_scale;
}

}  // namespace bt
