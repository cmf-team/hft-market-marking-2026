#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <memory>
#include "common/hft_types.hpp"

namespace hft_backtest {

// Structure to hold order book snapshot data
struct OrderBookSnapshot {
    uint64_t timestamp_us;  // Microseconds since epoch
    std::vector<std::pair<Price, Quantity>> bids;  // Price in cents, sorted descending
    std::vector<std::pair<Price, Quantity>> asks;  // Price in cents, sorted ascending
    
    Price get_best_bid() const {
        return bids.empty() ? 0 : bids[0].first;
    }
    
    Price get_best_ask() const {
        return asks.empty() ? 0 : asks[0].first;
    }
    
    Price get_mid_price() const {
        Price bid = get_best_bid();
        Price ask = get_best_ask();
        return (bid > 0 && ask > 0) ? (bid + ask) / 2 : 0;
    }
};

// Structure to hold trade data
struct TradeData {
    uint64_t timestamp_us;
    OrderSide side;
    Price price;  // Price in cents
    Quantity quantity;
    
    TradeData(uint64_t ts, OrderSide s, Price p, Quantity q)
        : timestamp_us(ts), side(s), price(p), quantity(q) {}
};

class BacktestDataReader {
public:
    BacktestDataReader() = default;
    ~BacktestDataReader() = default;
    
    // Load order book data from CSV file
    bool load_order_book_data(const std::string& filename);
    
    // Load trade data from CSV file  
    bool load_trade_data(const std::string& filename);
    
    // Get all order book snapshots (sorted by timestamp)
    const std::vector<OrderBookSnapshot>& get_order_book_snapshots() const {
        return order_book_snapshots_;
    }
    
    // Get all trades (sorted by timestamp)
    const std::vector<TradeData>& get_trades() const {
        return trades_;
    }
    
    // Get time range of loaded data
    std::pair<uint64_t, uint64_t> get_time_range() const;
    
    // Clear all loaded data
    void clear();
    
    // Get data statistics
    size_t get_order_book_count() const { return order_book_snapshots_.size(); }
    size_t get_trade_count() const { return trades_.size(); }

private:
    std::vector<OrderBookSnapshot> order_book_snapshots_;
    std::vector<TradeData> trades_;
    
    // Helper functions for parsing CSV
    std::vector<std::string> split_csv_line(const std::string& line) const;
    bool parse_order_book_line(const std::string& line, OrderBookSnapshot& snapshot) const;
    bool parse_trade_line(const std::string& line, TradeData& trade) const;
    OrderSide parse_side(const std::string& side_str) const;
    
    // Helper function to convert string to number
    template<typename T>
    bool safe_convert(const std::string& str, T& value) const;
    
    // Convert price from double to cents (uint64_t)
    Price price_to_cents(double price) const {
        return static_cast<Price>(price * 10000);  // 4 decimal places precision
    }
};

// Template implementation
template<typename T>
bool BacktestDataReader::safe_convert(const std::string& str, T& value) const {
    if (str.empty()) {
        return false;
    }
    
    try {
        if constexpr (std::is_same_v<T, double>) {
            value = std::stod(str);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            value = std::stoull(str);
        } else if constexpr (std::is_same_v<T, int>) {
            value = std::stoi(str);
        } else {
            return false;
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace hft_backtest