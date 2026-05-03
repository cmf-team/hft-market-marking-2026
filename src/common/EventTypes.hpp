#pragma once
#include "BasicTypes.hpp"
#include <variant>           // <-- required

namespace cmf {

struct LOBEvent {
    NanoTime timestamp;
    Side side;        // Buy (bid level) or Sell (ask level)
    Price price;
    Quantity qty;
};

struct TradeEvent {
    NanoTime timestamp;
    Price price;
    Quantity qty;
    Side aggressor;   // side of the market order (Buy = lifted ask, Sell = hit bid)
};

using Event = std::variant<LOBEvent, TradeEvent>;

inline NanoTime getTimestamp(const Event& e) {
    return std::visit([](const auto& ev) { return ev.timestamp; }, e);
}

} // namespace cmf