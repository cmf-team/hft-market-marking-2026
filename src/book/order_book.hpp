#pragma once

#include "common/BasicTypes.hpp"

#include <array>
#include <cstddef>

namespace cmf {

struct PriceLevel {
    Price    price{0.0};
    Quantity amount{0.0};
};

class OrderBook {
public:
    static constexpr std::size_t kDepth = 25;

    OrderBook() = default;

    void update(NanoTime                          ts,
                const std::array<PriceLevel, kDepth>&  asks,
                const std::array<PriceLevel, kDepth>&  bids);

    NanoTime timestamp() const noexcept { return ts_; }
    void          set_timestamp(NanoTime ts) noexcept { ts_ = ts; }

    std::array<PriceLevel, kDepth>& asks() noexcept { return asks_; }
    std::array<PriceLevel, kDepth>& bids() noexcept { return bids_; }

    PriceLevel best_ask() const noexcept;
    PriceLevel best_bid() const noexcept;

    Price mid_price() const noexcept;
    Price spread() const noexcept;

    PriceLevel ask_at(std::size_t level) const;
    PriceLevel bid_at(std::size_t level) const;

    bool empty() const noexcept;

private:
    NanoTime                   ts_{0};
    std::array<PriceLevel, kDepth>  asks_{};
    std::array<PriceLevel, kDepth>  bids_{};
};

}  // namespace cmf
