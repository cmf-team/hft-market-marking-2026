#include "exchange.hpp"

#include <algorithm>

namespace hft {

ExchangeEmulator::ExchangeEmulator(bool fill_on_touch)
    : fill_on_touch_(fill_on_touch) {}

bool ExchangeEmulator::crosses(Side side, double limit_price,
                               double market_price) const {
    if (limit_price <= 0.0 || market_price <= 0.0) {
        return false;
    }
    if (fill_on_touch_) {

        return side == Side::Buy ? market_price <= limit_price
                                 : market_price >= limit_price;
    }

    return side == Side::Buy ? market_price < limit_price
                             : market_price > limit_price;
}

double ExchangeEmulator::market_price_for_side(Side side) const {
    return side == Side::Buy ? best_ask_ : best_bid_;
}

void ExchangeEmulator::on_book(const BookEvent& event, std::vector<Fill>& fills) {
    best_bid_ = event.best_bid;
    best_ask_ = event.best_ask;
    has_book_ = best_bid_ > 0.0 && best_ask_ > 0.0;
    fill_crossed_orders_from_book(event.ts, fills);
}

void ExchangeEmulator::on_trade(const TradeEvent& event, std::vector<Fill>& fills) {
    fill_crossed_orders_from_trade(event.price, event.ts, fills);
}

std::uint64_t ExchangeEmulator::submit_order(const OrderRequest& request,
                                             Timestamp now,
                                             std::vector<Fill>& fills) {
    const std::uint64_t order_id = next_order_id_++;
    if (request.qty <= 0.0) {
        return order_id;
    }

    if (request.type == OrderType::Market) {
        if (!has_book_) {
            return order_id;
        }
        const double fill_price = market_price_for_side(request.side);
        if (fill_price <= 0.0) {
            return order_id;
        }

        fills.push_back(
            Fill{order_id, request.side, request.qty, fill_price, now, false});
        return order_id;
    }

    const Order order{order_id, request.side, request.type, request.qty,
                      request.price, now, OrderStatus::Open};


    if (has_book_ && crosses(order.side, order.price, market_price_for_side(order.side))) {
        fills.push_back(
            Fill{order.id, order.side, order.qty, order.price, now, false});
        return order_id;
    }

    open_orders_.emplace(order.id, order);
    return order_id;
}

bool ExchangeEmulator::cancel_order(std::uint64_t order_id) {
    const auto it = open_orders_.find(order_id);
    if (it == open_orders_.end()) {
        return false;
    }
    open_orders_.erase(it);
    return true;
}

std::size_t ExchangeEmulator::cancel_all() {
    const std::size_t count = open_orders_.size();
    open_orders_.clear();
    return count;
}

bool ExchangeEmulator::has_open_order(std::uint64_t order_id) const {
    return open_orders_.find(order_id) != open_orders_.end();
}

const Order* ExchangeEmulator::get_open_order(std::uint64_t order_id) const {
    const auto it = open_orders_.find(order_id);
    if (it == open_orders_.end()) {
        return nullptr;
    }
    return &it->second;
}

double ExchangeEmulator::mid_price() const {
    if (!has_book_) {
        return 0.0;
    }
    return (best_bid_ + best_ask_) * 0.5;
}

void ExchangeEmulator::fill_crossed_orders_from_book(Timestamp now,
                                                      std::vector<Fill>& fills) {
    if (!has_book_ || open_orders_.empty()) {
        return;
    }

    std::vector<std::uint64_t> to_remove;
    to_remove.reserve(open_orders_.size());

    for (const auto& [id, order] : open_orders_) {
        const double market = market_price_for_side(order.side);
        if (crosses(order.side, order.price, market)) {

            fills.push_back(Fill{id, order.side, order.qty, order.price, now, true});
            to_remove.push_back(id);
        }
    }

    for (std::uint64_t id : to_remove) {
        open_orders_.erase(id);
    }
}

void ExchangeEmulator::fill_crossed_orders_from_trade(double trade_price,
                                                       Timestamp now,
                                                       std::vector<Fill>& fills) {
    if (trade_price <= 0.0 || open_orders_.empty()) {
        return;
    }

    std::vector<std::uint64_t> to_remove;
    to_remove.reserve(open_orders_.size());

    for (const auto& [id, order] : open_orders_) {
        if (crosses(order.side, order.price, trade_price)) {

            fills.push_back(Fill{id, order.side, order.qty, order.price, now, true});
            to_remove.push_back(id);
        }
    }

    for (std::uint64_t id : to_remove) {
        open_orders_.erase(id);
    }
}

}
