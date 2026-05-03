#include "simple_cycle_strategy.hpp"

#include <algorithm>

namespace hft {

SimpleCycleStrategy::SimpleCycleStrategy(double order_qty, double take_profit_bps,
                                         std::int64_t entry_refresh_us,
                                         std::int64_t max_position)
    : order_qty_(order_qty),
      take_profit_ratio_(take_profit_bps * 1e-4),
      entry_refresh_us_(entry_refresh_us),
      max_position_(max_position) {}

StrategyActions SimpleCycleStrategy::on_book(const BookEvent& event,
                                             const ExchangeEmulator& exchange,
                                             const StrategyContext& context) {
    (void)exchange;
    StrategyActions actions;
    if (event.best_bid <= 0.0 || event.best_ask <= 0.0) {
        return actions;
    }


    mode_ = context.position > 0.0 ? Mode::SeekingExit : Mode::SeekingEntry;


    const bool should_requote =
        last_quote_ts_ == 0 ||
        entry_refresh_us_ <= 0 ||
        (event.ts - last_quote_ts_) >= entry_refresh_us_;
    if (!should_requote) {
        return actions;
    }


    actions.cancel_all = true;

    if (mode_ == Mode::SeekingEntry &&
        context.position + order_qty_ <= static_cast<double>(max_position_)) {
        OrderRequest order;
        order.side = Side::Buy;
        order.type = OrderType::Limit;
        order.qty = order_qty_;
        order.price = event.best_bid;
        actions.new_orders.push_back(order);
    } else if (mode_ == Mode::SeekingExit && context.position > 0.0) {

        const double base_price =
            last_entry_price_ > 0.0 ? last_entry_price_ : event.best_ask;

        OrderRequest order;
        order.side = Side::Sell;
        order.type = OrderType::Limit;
        order.qty = std::min(context.position, order_qty_);
        order.price = base_price * (1.0 + take_profit_ratio_);
        actions.new_orders.push_back(order);
    }

    last_quote_ts_ = event.ts;
    return actions;
}

StrategyActions SimpleCycleStrategy::on_trade(const TradeEvent& event,
                                              const ExchangeEmulator& exchange,
                                              const StrategyContext& context) {

    (void)event;
    (void)exchange;
    (void)context;
    return {};
}

void SimpleCycleStrategy::on_fill(const Fill& fill, const ExchangeEmulator& exchange,
                                  const StrategyContext& context) {
    (void)exchange;
    if (fill.side == Side::Buy) {
        last_entry_price_ = fill.price;
        mode_ = Mode::SeekingExit;
    } else {

        if (context.position <= 0.0) {
            mode_ = Mode::SeekingEntry;
            last_entry_price_ = 0.0;
        }
    }


    last_quote_ts_ = 0;
}

}
