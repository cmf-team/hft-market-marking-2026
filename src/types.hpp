#pragma once

#include <cstdint>
#include <variant>
#include <vector>

namespace hft {

using OrderId = std::uint64_t;
using Timestamp = std::int64_t;

inline constexpr OrderId INVALID_ORDER_ID = 0;

/**
 * @brief Identifies the side of an order, trade, or fill.
 */
enum class Side {
  Buy,
  Sell,
};

/**
 * @brief Returns the opposite side for an order or fill.
 * @param side Input side to invert.
 * @return Buy for Sell, or Sell for Buy.
 */
Side oppositeSide(Side side) noexcept;

/**
 * @brief Stores one price level in an order book snapshot.
 */
struct OrderBookEntry {
  double price{0.0};
  double amount{0.0};
};

/**
 * @brief Stores a limit order book snapshot at one timestamp.
 */
struct LOBData {
  Timestamp ts{0};
  std::vector<OrderBookEntry> asks;
  std::vector<OrderBookEntry> bids;
};

/**
 * @brief Stores one trade print from the market data stream.
 */
struct TradeData {
  Timestamp ts{0};
  Side aggressor_side{Side::Buy};
  double price{0.0};
  double amount{0.0};
};

/**
 * @brief Stores one simulated resting limit order.
 */
struct LimitOrder {
  OrderId id{INVALID_ORDER_ID};
  Side side{Side::Buy};
  double price{0.0};
  double initial_amount{0.0};
  double remaining_amount{0.0};
  double queue_ahead{0.0};
  Timestamp placed_at{0};
};

/**
 * @brief Stores one simulated execution fill.
 */
struct Fill {
  OrderId order_id{INVALID_ORDER_ID};
  Side side{Side::Buy};
  double price{0.0};
  double amount{0.0};
  Timestamp ts{0};
  bool is_taker{false};
};

using MarketEvent = std::variant<LOBData, TradeData>;

/**
 * @brief Returns the timestamp carried by a market event variant.
 * @param market_event Input event holding a book snapshot or trade print.
 * @return Event timestamp.
 */
Timestamp eventTs(const MarketEvent &market_event) noexcept;

}
