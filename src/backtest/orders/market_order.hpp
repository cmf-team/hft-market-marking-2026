#pragma once

#include "order.hpp"

namespace backtest {

class MarketOrder final : public Order {
public:
    MarketOrder(uint64_t id,
                uint64_t ts,
                Side side,
                uint64_t qty)
        : Order(id, ts, side, qty) {}

    bool is_market() const noexcept override {
        return true;
    }
};

}