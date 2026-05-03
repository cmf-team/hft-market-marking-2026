#pragma once

#include <memory>
#include <functional>
#include "backtest/orders/order.hpp"
#include "data_ingestion/data_types.hpp"
#include "analytics.hpp"

// Assume these exist from dataloading


namespace strategy {

using SubmitOrderFn = std::function<void(backtest::OrderPtr)>;
using CancelOrderFn = std::function<void(uint64_t order_id)>;

class IStrategy {
public:
    virtual ~IStrategy() = default;

    virtual void on_trade(const data::Trade& trade) = 0;

    virtual void on_order_book(const data::OrderBookSnapshot& book) = 0;

    virtual void on_order_update(const backtest::Order& order) = 0;

    virtual Analytics calculate_analytics() const = 0;

    virtual void set_order_submitter(SubmitOrderFn fn) = 0;

    virtual void set_order_canceller(CancelOrderFn fn) = 0;
};

} // namespace strategy