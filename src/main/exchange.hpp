#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "types.hpp"

namespace hft {

enum class OrderStatus { Open, Filled, Cancelled };


struct OrderRequest {
    Side side = Side::Buy;
    OrderType type = OrderType::Limit;
    double qty = 0.0;
    double price = 0.0;
};


struct Order {
    std::uint64_t id = 0;
    Side side = Side::Buy;
    OrderType type = OrderType::Limit;
    double qty = 0.0;
    double price = 0.0;
    Timestamp ts_submitted = 0;
    OrderStatus status = OrderStatus::Open;
};


struct Fill {
    std::uint64_t order_id = 0;
    Side side = Side::Buy;
    double qty = 0.0;
    double price = 0.0;
    Timestamp ts = 0;
    bool maker = true;
};


class ExchangeEmulator {
   public:
    explicit ExchangeEmulator(bool fill_on_touch);


    void on_book(const BookEvent& event, std::vector<Fill>& fills);
    void on_trade(const TradeEvent& event, std::vector<Fill>& fills);


    std::uint64_t submit_order(const OrderRequest& request, Timestamp now,
                               std::vector<Fill>& fills);
    bool cancel_order(std::uint64_t order_id);
    std::size_t cancel_all();

    bool has_open_order(std::uint64_t order_id) const;
    const Order* get_open_order(std::uint64_t order_id) const;

    bool has_book() const { return has_book_; }
    double best_bid() const { return best_bid_; }
    double best_ask() const { return best_ask_; }
    double mid_price() const;

    std::size_t open_order_count() const { return open_orders_.size(); }

   private:

    bool crosses(Side side, double limit_price, double market_price) const;
    double market_price_for_side(Side side) const;
    void fill_crossed_orders_from_book(Timestamp now, std::vector<Fill>& fills);
    void fill_crossed_orders_from_trade(double trade_price, Timestamp now,
                                        std::vector<Fill>& fills);

    bool fill_on_touch_ = true;
    bool has_book_ = false;
    double best_bid_ = 0.0;
    double best_ask_ = 0.0;
    std::unordered_map<std::uint64_t, Order> open_orders_;
    std::uint64_t next_order_id_ = 1;
};

}
