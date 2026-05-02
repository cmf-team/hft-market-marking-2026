#pragma once

#include "common/BasicTypes.hpp"
#include "core/event.hpp"
#include "exec/fill.hpp"

#include <cstddef>
#include <vector>

namespace cmf {

// Trade-based optimistic matching for at most one resting limit per side.
// Resting bid fills when an observed market sell prints at price <= our bid.
// Resting ask fills when an observed market buy prints at price >= our ask.
// Fill at our limit price (passive maker), amount = min(remaining, trade size).
// No queue position, no latency, no fees.
class MatchingEngine {
public:
    struct Order {
        Side     side{Side::Buy};
        Price    price{0.0};
        Quantity remaining{0.0};
        NanoTime placed_ts{0};
        bool          active{false};
    };

    MatchingEngine() = default;

    // Replace the resting order on `side` with a new limit at price/amount.
    // amount <= 0 cancels.
    void place(Side side, Price price, Quantity amount, NanoTime ts);
    void cancel(Side side);

    // Apply a market trade. Any fills of our orders are appended to `out`.
    void on_trade(const Trade& trade, std::vector<Fill>& out);

    const Order& bid() const noexcept { return bid_; }
    const Order& ask() const noexcept { return ask_; }
    std::size_t  fills_emitted() const noexcept { return fills_emitted_; }

private:
    Order bid_{Side::Buy,  0.0, 0.0, 0, false};
    Order ask_{Side::Sell, 0.0, 0.0, 0, false};
    std::size_t fills_emitted_{0};
};

}  // namespace cmf
