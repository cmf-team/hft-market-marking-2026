#pragma once

#include "order.hpp"

namespace backtest {

class LimitOrder final : public Order {
public:
    LimitOrder(uint64_t id,
               uint64_t ts,
               Side side,
               uint64_t qty,
               uint64_t price)
        : Order(id, ts, side, qty),
          price_(price) {}

    uint64_t price() const noexcept { return price_; }

    bool is_market() const noexcept override {
        return false;
    }

private:
    uint64_t price_;
};

}