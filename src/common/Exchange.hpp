#pragma once

#include "common/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace cmf
{

class Exchange
{
  public:
    explicit Exchange(bool partial_fills = true) : partial_fills_(partial_fills) {}

    void onLobUpdate(const LOBSnapshot& lob);
    void onTrade(const Trade& trade);

    OrderId placeOrder(Side side, OrderType type, Price price, Quantity quantity);
    bool cancelOrder(OrderId order_id);
    void cancelAll();

    std::vector<Fill> pollFills();

    Price bestBid() const;
    Price bestAsk() const;
    Price midPrice() const;
    Price spread() const;
    bool hasMarketData() const { return has_data_; }
    MicroTime currentTime() const { return current_time_; }
    const LOBSnapshot& currentLob() const { return current_lob_; }
    std::size_t activeOrderCount() const { return active_orders_.size(); }

  private:
    bool partial_fills_ = true;

    LOBSnapshot current_lob_{};
    bool has_data_ = false;
    MicroTime current_time_ = 0;

    OrderId next_order_id_ = 1;
    std::map<OrderId, Order> active_orders_;
    std::vector<Fill> pending_fills_;

    Quantity executeAgainstBook(Order& order);

    // Match resting limits against an aggressive trade event.
    void matchAgainstTrade(const Trade& trade);

    void matchOrders();
};

} // namespace cmf
