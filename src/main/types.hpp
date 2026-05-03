#pragma once

#include <cstdint>
#include <string>

namespace hft {


using Timestamp = std::int64_t;

enum class Side { Buy, Sell };

enum class OrderType { Limit, Market };


inline Side side_from_string(const std::string& value) {
    if (value == "buy" || value == "BUY" || value == "Buy") {
        return Side::Buy;
    }
    return Side::Sell;
}

inline const char* to_string(Side side) {
    return side == Side::Buy ? "buy" : "sell";
}


struct BookEvent {
    Timestamp ts = 0;
    double best_ask = 0.0;
    double best_ask_qty = 0.0;
    double best_bid = 0.0;
    double best_bid_qty = 0.0;
};


struct TradeEvent {
    Timestamp ts = 0;
    Side side = Side::Buy;
    double price = 0.0;
    double qty = 0.0;
};

}
