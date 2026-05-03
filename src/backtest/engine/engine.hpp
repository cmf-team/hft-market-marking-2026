#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <vector>

#include "../orders/order.hpp"
#include "../orders/limit_order.hpp"
#include "../strategy/strategy.hpp"

namespace backtest {

class BacktestEngine {
public:
    explicit BacktestEngine(strategy::IStrategy& strategy)
        : strategy_(strategy) {
        strategy_.set_order_submitter(
            [this](OrderPtr order) {
                submit_order(std::move(order));
            }
        );
        strategy_.set_order_canceller(
            [this](uint64_t id) {
                cancel_order(id);
            }
        );
    }

    void run(std::span<const data::Event> events) {
        for (const auto& ev : events) {
            activate_waiting_orders(ev.local_timestamp);

            std::visit([this](const auto& e) {
                handle_event(e);
            }, ev.data);
        }
    }

private:
    strategy::IStrategy& strategy_;
    std::deque<OrderPtr> all_orders_;
    std::deque<OrderPtr> active_orders_;
    std::deque<OrderPtr> waiting_orders_;

    std::map<uint64_t, std::deque<OrderPtr>, std::greater<>> buy_limit_;
    std::map<uint64_t, std::deque<OrderPtr>> sell_limit_;

    std::map<uint64_t, uint64_t, std::greater<>> bids_;
    std::map<uint64_t, uint64_t> asks_;

    void submit_order(OrderPtr order) {
        waiting_orders_.emplace_back(std::move(order));
    }

    void cancel_order(uint64_t id) {
        for (auto& op : waiting_orders_) {
            if (op->id() == id) { op->cancel(); strategy_.on_order_update(*op); return; }
        }
        for (auto& op : active_orders_) {
            if (op->id() == id) { op->cancel(); strategy_.on_order_update(*op); return; }
        }
    }

    void activate_waiting_orders(uint64_t ts) {
        auto it = waiting_orders_.begin();
        while (it != waiting_orders_.end()) {
            if ((*it)->timestamp() <= ts) {
                if ((*it)->is_market()){
                    fill_market_order(**it);
                }
                active_orders_.emplace_back(std::move(*it));
                it = waiting_orders_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void handle_event(const data::Trade& t) {
        match_active_orders_against_trade(t);
        strategy_.on_trade(t);
    }

    void handle_event(const data::OrderBookSnapshot& ob) {
        rebuild_book(ob);
        strategy_.on_order_book(ob);
    }

    void handle_event(const auto&) {}

    void rebuild_book(const data::OrderBookSnapshot& ob) {
        bids_.clear();
        asks_.clear();

        for (std::size_t i = 0; i < ob.bids.size(); ++i) {
            bids_[ob.bids[i].price] = ob.bids[i].amount;
        }

        for (std::size_t i = 0; i < ob.asks.size(); ++i) {
            asks_[ob.asks[i].price] += ob.asks[i].amount;
        }
    }

    void match_active_orders_against_trade(const data::Trade& t) {
        auto it = active_orders_.begin();
        while (it != active_orders_.end()) {
            auto& order = *it;

            if (order->status() == OrderStatus::Filled ||
                order->status() == OrderStatus::Cancelled) {
                all_orders_.emplace_back(std::move(*it));
                it = active_orders_.erase(it);
                continue;
            }

            fill_limit_order(static_cast<LimitOrder&>(*order), t);

            if (order->status() == OrderStatus::Filled) {
                all_orders_.emplace_back(std::move(*it));
                it = active_orders_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void fill_market_order(Order& order) {
        uint64_t remaining = order.remaining();

        auto fill_from = [&](auto& book) {
            for (auto it = book.begin(); it != book.end() && remaining > 0; ++it) {
                uint64_t fill_qty = std::min(remaining, it->second);
                order.fill(fill_qty);
                remaining -= fill_qty;
                strategy_.on_order_update(order);
            }
        };

        if (order.side() == Side::Buy)
            fill_from(asks_);
        else
            fill_from(bids_);
    }

    void fill_limit_order(LimitOrder& order, const data::Trade& t) {
        if (order.remaining() == 0) return;

        const bool crosses =
            (order.side() == Side::Buy  && t.px_qty.price <= order.price()) ||
            (order.side() == Side::Sell && t.px_qty.price >= order.price());

        if (!crosses) return;

        const uint64_t fill_qty = std::min(order.remaining(), t.px_qty.amount);
        if (fill_qty == 0) return;

        order.fill(fill_qty);
        strategy_.on_order_update(order);
    }
};

} // namespace backtest