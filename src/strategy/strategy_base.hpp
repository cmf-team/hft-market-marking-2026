#pragma once

#include "book/order_book.hpp"
#include "core/event.hpp"
#include "exec/fill.hpp"

namespace cmf {

class StrategyBase {
public:
    virtual ~StrategyBase() = default;

    virtual void on_book_update(const OrderBook& /*book*/) {}
    virtual void on_trade(const Trade& /*trade*/) {}
    virtual void on_fill(const Fill& /*fill*/) {}
};

}  // namespace cmf
