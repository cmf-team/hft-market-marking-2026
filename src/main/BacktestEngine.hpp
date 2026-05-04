#pragma once
#include "common/BasicTypes.hpp"
#include <iomanip>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace cmf
{

class BacktestEngine
{
  private:
    struct VirtualOrder
    {
        Side side;
        Price price;
        Quantity size;
    };

    std::unordered_map<OrderId, VirtualOrder> active_orders_;
    OrderId next_oid_ = 1;

    Price cur_bid_ = 0;
    Quantity cur_bid_size_ = 0;
    Price cur_ask_ = 0;
    Quantity cur_ask_size_ = 0;

    double inventory_ = 0.0;
    double cash_ = 0.0;
    double turnover_ = 0.0;
    int trades_count_ = 0;

  public:
    Price getBestBid() const { return cur_bid_; }
    Price getBestAsk() const { return cur_ask_; }
    Quantity getBidSize() const { return cur_bid_size_; }
    Quantity getAskSize() const { return cur_ask_size_; }
    double getInventory() const { return inventory_; }

    void clearOrders() { active_orders_.clear(); }

    void placeOrder(Side side, Price price, Quantity size)
    {
        active_orders_[next_oid_++] = {side, price, size};
    }

    void processEvent(const MarketEvent& ev)
    {
        if (ev.type == EventType::Quote)
        {
            cur_bid_ = ev.best_bid_price;
            cur_bid_size_ = ev.best_bid_size;
            cur_ask_ = ev.best_ask_price;
            cur_ask_size_ = ev.best_ask_size;
            matchAgainstQuotes();
        }
        else if (ev.type == EventType::Trade)
        {
            matchAgainstTrade(ev.trade_price, ev.trade_side, ev.trade_size);
        }
    }

  private:
    auto fillOrder(std::unordered_map<OrderId, VirtualOrder>::iterator it)
    {
        if (it->second.side == Side::Buy)
        {
            inventory_ += it->second.size;
            cash_ -= it->second.size * it->second.price;
        }
        else
        {
            inventory_ -= it->second.size;
            cash_ += it->second.size * it->second.price;
        }
        turnover_ += it->second.size;
        trades_count_++;
        return active_orders_.erase(it);
    }

    void matchAgainstQuotes()
    {
        if (cur_bid_ == 0 || cur_ask_ == 0)
            return;
        for (auto it = active_orders_.begin(); it != active_orders_.end();)
        {
            if (it->second.side == Side::Buy && cur_ask_ <= it->second.price)
            {
                it = fillOrder(it);
            }
            else if (it->second.side == Side::Sell && cur_bid_ >= it->second.price)
            {
                it = fillOrder(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void matchAgainstTrade(Price trade_px, Side aggressor_side, Quantity trade_sz)
    {
        (void)trade_sz;
        for (auto it = active_orders_.begin(); it != active_orders_.end();)
        {
            if (aggressor_side == Side::Sell && it->second.side == Side::Buy && trade_px <= it->second.price)
            {
                it = fillOrder(it);
            }
            else if (aggressor_side == Side::Buy && it->second.side == Side::Sell && trade_px >= it->second.price)
            {
                it = fillOrder(it);
            }
            else
            {
                ++it;
            }
        }
    }

  public:
    void printReport(const std::string& strategy_name)
    {
        double mid = (cur_bid_ + cur_ask_) / 2.0;
        double unrpnl = inventory_ * mid;
        double total_pnl = cash_ + unrpnl;

        std::cout << "========== " << strategy_name << " ==========\n";
        std::cout << "Final Inventory:  " << std::fixed << std::setprecision(2) << inventory_ << "\n";
        std::cout << "Total Volume:     " << turnover_ << " contracts\n";
        std::cout << "Number of Trades: " << trades_count_ << "\n";
        std::cout << "Realized PnL:     " << cash_ << "\n";
        std::cout << "Unrealized PnL:   " << unrpnl << "\n";
        std::cout << "Total PnL:        " << total_pnl << "\n\n";
    }
};

} // namespace cmf