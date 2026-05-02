#include "backtest/backtest_data_reader.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>

namespace hft_backtest {

bool BacktestDataReader::load_order_book_data(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open order book file: " << filename << std::endl;
        return false;
    }
    
    std::string line;
    bool first_line = true;
    size_t line_count = 0;
    
    while (std::getline(file, line)) {
        line_count++;
        
        // Skip header line
        if (first_line) {
            first_line = false;
            continue;
        }
        
        if (line.empty()) {
            continue;
        }
        
        OrderBookSnapshot snapshot;
        if (parse_order_book_line(line, snapshot)) {
            order_book_snapshots_.push_back(snapshot);
        } else {
            std::cerr << "Warning: Failed to parse order book line " << line_count << std::endl;
        }
        
        // Progress indicator for large files
        if (line_count % 100000 == 0) {
            std::cout << "Processed " << line_count << " order book lines..." << std::endl;
        }
    }
    
    // Sort by timestamp
    std::sort(order_book_snapshots_.begin(), order_book_snapshots_.end(),
              [](const OrderBookSnapshot& a, const OrderBookSnapshot& b) {
                  return a.timestamp_us < b.timestamp_us;
              });
    
    std::cout << "Loaded " << order_book_snapshots_.size() << " order book snapshots" << std::endl;
    return true;
}

bool BacktestDataReader::load_trade_data(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open trade file: " << filename << std::endl;
        return false;
    }
    
    std::string line;
    bool first_line = true;
    size_t line_count = 0;
    
    while (std::getline(file, line)) {
        line_count++;
        
        // Skip header line
        if (first_line) {
            first_line = false;
            continue;
        }
        
        if (line.empty()) {
            continue;
        }
        
        TradeData trade(0, OrderSide::BUY, 0, 0);
        if (parse_trade_line(line, trade)) {
            trades_.push_back(trade);
        } else {
            std::cerr << "Warning: Failed to parse trade line " << line_count << std::endl;
        }
        
        // Progress indicator for large files
        if (line_count % 1000000 == 0) {
            std::cout << "Processed " << line_count << " trade lines..." << std::endl;
        }
    }
    
    // Sort by timestamp
    std::sort(trades_.begin(), trades_.end(),
              [](const TradeData& a, const TradeData& b) {
                  return a.timestamp_us < b.timestamp_us;
              });
    
    std::cout << "Loaded " << trades_.size() << " trades" << std::endl;
    return true;
}

std::pair<uint64_t, uint64_t> BacktestDataReader::get_time_range() const {
    if (order_book_snapshots_.empty() && trades_.empty()) {
        return {0, 0};
    }
    
    uint64_t min_time = UINT64_MAX;
    uint64_t max_time = 0;
    
    if (!order_book_snapshots_.empty()) {
        min_time = std::min(min_time, order_book_snapshots_.front().timestamp_us);
        max_time = std::max(max_time, order_book_snapshots_.back().timestamp_us);
    }
    
    if (!trades_.empty()) {
        min_time = std::min(min_time, trades_.front().timestamp_us);
        max_time = std::max(max_time, trades_.back().timestamp_us);
    }
    
    return {min_time, max_time};
}

void BacktestDataReader::clear() {
    order_book_snapshots_.clear();
    trades_.clear();
}

std::vector<std::string> BacktestDataReader::split_csv_line(const std::string& line) const {
    std::vector<std::string> tokens;
    std::stringstream ss(line);
    std::string token;
    
    while (std::getline(ss, token, ',')) {
        tokens.push_back(token);
    }
    
    return tokens;
}

bool BacktestDataReader::parse_order_book_line(const std::string& line, OrderBookSnapshot& snapshot) const {
    auto tokens = split_csv_line(line);
    
    // Expected format: index, timestamp, asks[0].price, asks[0].amount, bids[0].price, bids[0].amount, ...
    // Total columns should be 102: index(1) + timestamp(1) + 25*2*2 (asks+bids, price+amount)
    if (tokens.size() != 102) {
        return false;
    }
    
    // Parse timestamp (column 1)
    if (!safe_convert(tokens[1], snapshot.timestamp_us)) {
        return false;
    }
    
    // Parse 25 levels of asks and bids
    snapshot.asks.clear();
    snapshot.bids.clear();
    snapshot.asks.reserve(25);
    snapshot.bids.reserve(25);
    
    for (int level = 0; level < 25; ++level) {
        // Ask price and amount
        int ask_price_idx = 2 + level * 4;
        int ask_amount_idx = 2 + level * 4 + 1;
        int bid_price_idx = 2 + level * 4 + 2;
        int bid_amount_idx = 2 + level * 4 + 3;
        
        double ask_price_dbl, bid_price_dbl;
        double ask_amount_dbl, bid_amount_dbl;
        
        if (safe_convert(tokens[ask_price_idx], ask_price_dbl) && 
            safe_convert(tokens[ask_amount_idx], ask_amount_dbl) &&
            ask_price_dbl > 0 && ask_amount_dbl > 0) {
            Price ask_price = price_to_cents(ask_price_dbl);
            Quantity ask_amount = static_cast<Quantity>(ask_amount_dbl);
            snapshot.asks.emplace_back(ask_price, ask_amount);
        }
        
        if (safe_convert(tokens[bid_price_idx], bid_price_dbl) && 
            safe_convert(tokens[bid_amount_idx], bid_amount_dbl) &&
            bid_price_dbl > 0 && bid_amount_dbl > 0) {
            Price bid_price = price_to_cents(bid_price_dbl);
            Quantity bid_amount = static_cast<Quantity>(bid_amount_dbl);
            snapshot.bids.emplace_back(bid_price, bid_amount);
        }
    }
    
    // Sort asks ascending by price, bids descending by price
    std::sort(snapshot.asks.begin(), snapshot.asks.end(),
              [](const std::pair<Price, Quantity>& a, const std::pair<Price, Quantity>& b) {
                  return a.first < b.first;
              });
    
    std::sort(snapshot.bids.begin(), snapshot.bids.end(),
              [](const std::pair<Price, Quantity>& a, const std::pair<Price, Quantity>& b) {
                  return a.first > b.first;
              });
    
    return true;
}

bool BacktestDataReader::parse_trade_line(const std::string& line, TradeData& trade) const {
    auto tokens = split_csv_line(line);
    
    // Expected format: index, timestamp, side, price, amount
    if (tokens.size() != 5) {
        return false;
    }
    
    // Parse timestamp
    if (!safe_convert(tokens[1], trade.timestamp_us)) {
        return false;
    }
    
    // Parse side
    trade.side = parse_side(tokens[2]);
    
    // Parse price and amount
    double price_dbl, amount_dbl;
    if (!safe_convert(tokens[3], price_dbl) || 
        !safe_convert(tokens[4], amount_dbl)) {
        return false;
    }
    
    trade.price = price_to_cents(price_dbl);
    trade.quantity = static_cast<Quantity>(amount_dbl);
    
    return trade.price > 0 && trade.quantity > 0;
}

OrderSide BacktestDataReader::parse_side(const std::string& side_str) const {
    if (side_str == "buy" || side_str == "BUY") {
        return OrderSide::BUY;
    } else {
        return OrderSide::SELL;  // Default to SELL for "sell" or unknown strings
    }
}

} // namespace hft_backtest