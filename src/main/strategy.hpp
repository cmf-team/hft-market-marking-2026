#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "exchange.hpp"
#include "types.hpp"

namespace hft {


struct StrategyContext {
    Timestamp now = 0;
    double position = 0.0;
    double cash = 0.0;
    double mid_price = 0.0;
};


struct StrategyActions {
    bool cancel_all = false;
    std::vector<std::uint64_t> cancel_order_ids;
    std::vector<OrderRequest> new_orders;
};


class IStrategy {
   public:
    virtual ~IStrategy() = default;

    virtual std::string name() const = 0;


    virtual void on_start(const StrategyContext& context) { (void)context; }


    virtual StrategyActions on_book(const BookEvent& event,
                                    const ExchangeEmulator& exchange,
                                    const StrategyContext& context) = 0;


    virtual StrategyActions on_trade(const TradeEvent& event,
                                     const ExchangeEmulator& exchange,
                                     const StrategyContext& context) = 0;


    virtual void on_fill(const Fill& fill, const ExchangeEmulator& exchange,
                         const StrategyContext& context) {
        (void)fill;
        (void)exchange;
        (void)context;
    }
};

}
