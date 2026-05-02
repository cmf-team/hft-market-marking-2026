#include "execution/matching_engine.hpp"
#include "logging.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace hft::execution {

namespace {

constexpr double FLOAT_EPSILON = 1e-12;
constexpr double UNAVAILABLE_QUOTE_PRICE = -1.0;

using BidLevels = std::map<double, std::deque<OrderId>, std::greater<double>>;
using AskLevels = std::map<double, std::deque<OrderId>>;

std::string
formatActiveOrders(const std::unordered_map<OrderId, LimitOrder> &orders) {
  if (orders.empty()) {
    return "(none)";
  }
  std::vector<const LimitOrder *> rows;
  rows.reserve(orders.size());
  for (const auto &entry : orders)
    rows.push_back(&entry.second);
  std::sort(rows.begin(), rows.end(),
            [](const LimitOrder *lhs, const LimitOrder *rhs) {
              return lhs->id < rhs->id;
            });
  std::ostringstream o;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (i) {
      o << ' ';
    }
    const LimitOrder &limit_order = *rows[i];
    o << (limit_order.side == Side::Buy ? "BUY#" : "SELL#") << limit_order.id
      << '@' << limit_order.price << 'x' << limit_order.remaining_amount
      << " qAhead=" << limit_order.queue_ahead;
  }
  return o.str();
}

void logMatchDbgResting(Timestamp ts, const char *phase, const BidLevels &bids,
                        const AskLevels &asks,
                        const std::unordered_map<OrderId, LimitOrder> &orders) {
  const double bestBid = bids.empty() ? 0.0 : bids.begin()->first;
  const double bestAsk = asks.empty() ? 0.0 : asks.begin()->first;
  logging::Logger::debug(
      "[MATCHDBG] ts=", ts, ' ', phase,
      " our_bb=", (bids.empty() ? UNAVAILABLE_QUOTE_PRICE : bestBid),
      " our_ba=", (asks.empty() ? UNAVAILABLE_QUOTE_PRICE : bestAsk),
      " active=", formatActiveOrders(orders));
}

}

OrderId MatchingEngine::placeLimit(Side side, double price, double amount,
                                   Timestamp ts, double queueAhead) {
  if (amount <= FLOAT_EPSILON || price <= 0.0) {
    return INVALID_ORDER_ID;
  }

  LimitOrder order;
  order.id = next_id_++;
  order.side = side;
  order.price = price;
  order.initial_amount = amount;
  order.remaining_amount = amount;
  order.queue_ahead = std::max(queueAhead, 0.0);
  order.placed_at = ts;
  orders_[order.id] = order;

  if (side == Side::Buy) {
    bids_[price].push_back(order.id);
  } else {
    asks_[price].push_back(order.id);
  }

  logging::Logger::debug("[MATCH] place id=", order.id,
                         " side=", (side == Side::Buy ? "BUY" : "SELL"),
                         " price=", price, " amount=", amount,
                         " queue_ahead=", order.queue_ahead);
  return order.id;
}

void MatchingEngine::eraseFromLevels(const LimitOrder &order) {
  if (order.side == Side::Buy) {
    auto it = bids_.find(order.price);
    if (it == bids_.end()) {
      return;
    }
    auto &order_queue = it->second;
    order_queue.erase(
        std::remove(order_queue.begin(), order_queue.end(), order.id),
        order_queue.end());
    if (order_queue.empty()) {
      bids_.erase(it);
    }
  } else {
    auto it = asks_.find(order.price);
    if (it == asks_.end()) {
      return;
    }
    auto &order_queue = it->second;
    order_queue.erase(
        std::remove(order_queue.begin(), order_queue.end(), order.id),
        order_queue.end());
    if (order_queue.empty()) {
      asks_.erase(it);
    }
  }
}

bool MatchingEngine::cancel(OrderId id) {
  auto it = orders_.find(id);
  if (it == orders_.end()) {
    return false;
  }
  eraseFromLevels(it->second);
  orders_.erase(it);
  logging::Logger::debug("[MATCH] cancel id=", id);
  return true;
}

void MatchingEngine::cancelAll() {
  orders_.clear();
  bids_.clear();
  asks_.clear();
}

void MatchingEngine::cancelSide(Side side) {
  if (side == Side::Buy) {
    for (const auto &[price_level, order_queue] : bids_) {
      (void)price_level;
      for (OrderId order_id : order_queue)
        orders_.erase(order_id);
    }
    bids_.clear();
  } else {
    for (const auto &[price_level, order_queue] : asks_) {
      (void)price_level;
      for (OrderId order_id : order_queue)
        orders_.erase(order_id);
    }
    asks_.clear();
  }
}

void MatchingEngine::matchSellAggressor(const TradeData &trade,
                                        double &remainingTrade,
                                        std::vector<Fill> &fills) {
  while (remainingTrade > FLOAT_EPSILON && !bids_.empty()) {
    auto level_it = bids_.begin();
    const double level_price = level_it->first;
    if (level_price < trade.price) {
      break;
    }

    auto &order_queue = level_it->second;
    while (remainingTrade > FLOAT_EPSILON && !order_queue.empty()) {
      const OrderId order_id = order_queue.front();
      auto &resting_order = orders_[order_id];
      if (resting_order.queue_ahead > FLOAT_EPSILON) {
        const double consumed_ahead =
            std::min(resting_order.queue_ahead, remainingTrade);
        resting_order.queue_ahead -= consumed_ahead;
        remainingTrade -= consumed_ahead;
        if (remainingTrade <= FLOAT_EPSILON) {
          break;
        }
      }
      const double take =
          std::min(resting_order.remaining_amount, remainingTrade);
      resting_order.remaining_amount -= take;
      remainingTrade -= take;

      Fill fill;
      fill.order_id = order_id;
      fill.side = Side::Buy;
      fill.price = resting_order.price;
      fill.amount = take;
      fill.ts = trade.ts;
      fill.is_taker = false;
      fills.push_back(fill);

      if (resting_order.remaining_amount <= FLOAT_EPSILON) {
        order_queue.pop_front();
        orders_.erase(order_id);
      }
    }
    if (order_queue.empty()) {
      bids_.erase(level_it);
    }
  }
}

void MatchingEngine::matchBuyAggressor(const TradeData &trade,
                                       double &remainingTrade,
                                       std::vector<Fill> &fills) {
  while (remainingTrade > FLOAT_EPSILON && !asks_.empty()) {
    auto level_it = asks_.begin();
    const double level_price = level_it->first;
    if (level_price > trade.price) {
      break;
    }

    auto &order_queue = level_it->second;
    while (remainingTrade > FLOAT_EPSILON && !order_queue.empty()) {
      const OrderId order_id = order_queue.front();
      auto &resting_order = orders_[order_id];
      if (resting_order.queue_ahead > FLOAT_EPSILON) {
        const double consumed_ahead =
            std::min(resting_order.queue_ahead, remainingTrade);
        resting_order.queue_ahead -= consumed_ahead;
        remainingTrade -= consumed_ahead;
        if (remainingTrade <= FLOAT_EPSILON) {
          break;
        }
      }
      const double take =
          std::min(resting_order.remaining_amount, remainingTrade);
      resting_order.remaining_amount -= take;
      remainingTrade -= take;

      Fill fill;
      fill.order_id = order_id;
      fill.side = Side::Sell;
      fill.price = resting_order.price;
      fill.amount = take;
      fill.ts = trade.ts;
      fill.is_taker = false;
      fills.push_back(fill);

      if (resting_order.remaining_amount <= FLOAT_EPSILON) {
        order_queue.pop_front();
        orders_.erase(order_id);
      }
    }
    if (order_queue.empty()) {
      asks_.erase(level_it);
    }
  }
}

void MatchingEngine::logTradeNoFillHints(const TradeData &trade) const {
  if (trade.amount <= FLOAT_EPSILON) {
    return;
  }
  if (trade.aggressor_side == Side::Sell) {
    if (bids_.empty()) {
      logging::Logger::debug(
          "[MATCHDBG] ts=", trade.ts,
          " no fill: sell aggressor — need our BID at trade_px",
          " or better; we have no bids");
    } else {
      const double best_bid = bids_.begin()->first;
      if (best_bid < trade.price) {
        logging::Logger::debug(
            "[MATCHDBG] ts=", trade.ts,
            " no fill: sell aggressor trade_px=", trade.price,
            " > our_best_bid=", best_bid, " (need resting bid >= trade price)");
      }
    }
  } else {
    if (asks_.empty()) {
      logging::Logger::debug(
          "[MATCHDBG] ts=", trade.ts,
          " no fill: buy aggressor — need our ASK at trade_px",
          " or better; we have no asks");
    } else {
      const double best_ask = asks_.begin()->first;
      if (best_ask > trade.price) {
        logging::Logger::debug("[MATCHDBG] ts=", trade.ts,
                               " no fill: buy aggressor trade_px=", trade.price,
                               " < our_best_ask=", best_ask,
                               " (need resting ask <= trade price)");
      }
    }
  }
}

std::vector<Fill> MatchingEngine::onTrade(const TradeData &trade) {
  logging::Logger::debug(
      "[MATCHDBG] ts=", trade.ts, " EVENT=TRADE ",
      (trade.aggressor_side == Side::Sell ? "aggr=SELL" : "aggr=BUY"),
      " trade_px=", trade.price, " trade_sz=", trade.amount,
      " | passive side hit: ",
      (trade.aggressor_side == Side::Sell ? "our BIDs vs sell aggressor"
                                          : "our ASKs vs buy aggressor"));
  logMatchDbgResting(trade.ts, "before on_trade", bids_, asks_, orders_);

  std::vector<Fill> fills;
  double remainingTrade = trade.amount;

  if (trade.aggressor_side == Side::Sell) {
    matchSellAggressor(trade, remainingTrade, fills);
  } else {
    matchBuyAggressor(trade, remainingTrade, fills);
  }

  logging::Logger::debug("[MATCHDBG] ts=", trade.ts,
                         " on_trade done fills=", fills.size(),
                         " remaining_trade=", remainingTrade);
  if (!fills.empty() && remainingTrade > FLOAT_EPSILON) {
    logging::Logger::debug(
        "[MATCHDBG] ts=", trade.ts,
        " partial trade consumption: leftover aggressor size=", remainingTrade,
        " (we ran out of resting depth or stopped at price gap)");
  }
  if (fills.empty()) {
    logTradeNoFillHints(trade);
  }

  return fills;
}

void MatchingEngine::uncrossBidsWithExternalAsks(const LOBData &book,
                                                 std::vector<Fill> &fills) {
  while (!bids_.empty() && !book.asks.empty()) {
    const double extBestAsk = book.asks.front().price;
    auto level_it = bids_.begin();
    const double myBid = level_it->first;
    if (extBestAsk > myBid) {
      break;
    }

    auto &order_queue = level_it->second;
    while (!order_queue.empty()) {
      const OrderId order_id = order_queue.front();
      auto &resting_order = orders_[order_id];
      Fill fill;
      fill.order_id = order_id;
      fill.side = Side::Buy;
      fill.price = resting_order.price;
      fill.amount = resting_order.remaining_amount;
      fill.ts = book.ts;
      fill.is_taker = false;
      fills.push_back(fill);
      order_queue.pop_front();
      orders_.erase(order_id);
    }
    bids_.erase(level_it);
  }
}

void MatchingEngine::uncrossAsksWithExternalBids(const LOBData &book,
                                                 std::vector<Fill> &fills) {
  while (!asks_.empty() && !book.bids.empty()) {
    const double extBestBid = book.bids.front().price;
    auto level_it = asks_.begin();
    const double myAsk = level_it->first;
    if (extBestBid < myAsk) {
      break;
    }

    auto &order_queue = level_it->second;
    while (!order_queue.empty()) {
      const OrderId order_id = order_queue.front();
      auto &resting_order = orders_[order_id];
      Fill fill;
      fill.order_id = order_id;
      fill.side = Side::Sell;
      fill.price = resting_order.price;
      fill.amount = resting_order.remaining_amount;
      fill.ts = book.ts;
      fill.is_taker = false;
      fills.push_back(fill);
      order_queue.pop_front();
      orders_.erase(order_id);
    }
    asks_.erase(level_it);
  }
}

void MatchingEngine::logBookUncrossHints(const LOBData &book) const {
  if (!bids_.empty() && !book.asks.empty()) {
    const double our_best_bid = bids_.begin()->first;
    const double external_best_ask = book.asks.front().price;
    if (external_best_ask > our_best_bid) {
      logging::Logger::debug("[MATCHDBG] ts=", book.ts,
                             " no book cross buy-side: ext best_ask=",
                             external_best_ask, " > our_bid=", our_best_bid);
    }
  }
  if (!asks_.empty() && !book.bids.empty()) {
    const double our_best_ask = asks_.begin()->first;
    const double external_best_bid = book.bids.front().price;
    if (external_best_bid < our_best_ask) {
      logging::Logger::debug("[MATCHDBG] ts=", book.ts,
                             " no book cross sell-side: ext best_bid=",
                             external_best_bid, " < our_ask=", our_best_ask);
    }
  }
}

std::vector<Fill> MatchingEngine::onBookUpdate(const LOBData &book) {
  const double external_best_bid =
      book.bids.empty() ? UNAVAILABLE_QUOTE_PRICE : book.bids.front().price;
  const double external_best_ask =
      book.asks.empty() ? UNAVAILABLE_QUOTE_PRICE : book.asks.front().price;
  logging::Logger::debug("[MATCHDBG] ts=", book.ts,
                         " EVENT=BOOK ext_bb=", external_best_bid,
                         " ext_ba=", external_best_ask);
  logMatchDbgResting(book.ts, "before on_book_update", bids_, asks_, orders_);

  std::vector<Fill> fills;
  uncrossBidsWithExternalAsks(book, fills);
  uncrossAsksWithExternalBids(book, fills);

  logging::Logger::debug("[MATCHDBG] ts=", book.ts,
                         " on_book_update done fills=", fills.size());
  if (fills.empty()) {
    logBookUncrossHints(book);
  }

  return fills;
}

std::vector<Fill> MatchingEngine::executeMarket(Side side, double amount,
                                                const LOBData &book,
                                                Timestamp ts) {
  std::vector<Fill> fills;
  double remaining = amount;

  const auto &levels = (side == Side::Buy) ? book.asks : book.bids;
  for (const auto &level : levels) {
    if (remaining <= FLOAT_EPSILON) {
      break;
    }
    const double take = std::min(remaining, level.amount);
    Fill fill;
    fill.order_id = INVALID_ORDER_ID;
    fill.side = side;
    fill.price = level.price;
    fill.amount = take;
    fill.ts = ts;
    fill.is_taker = true;
    fills.push_back(fill);
    remaining -= take;
  }
  if (remaining > FLOAT_EPSILON) {
    logging::Logger::debug("[MATCH] market order short of liquidity, ",
                           "unfilled=", remaining);
  }
  return fills;
}

std::vector<LimitOrder> MatchingEngine::activeOrders() const {
  std::vector<LimitOrder> out;
  out.reserve(orders_.size());
  for (const auto &[order_id, limit_order] : orders_) {
    (void)order_id;
    out.push_back(limit_order);
  }
  return out;
}

std::vector<LimitOrder> MatchingEngine::activeOrders(Side side) const {
  std::vector<LimitOrder> out;
  for (const auto &[order_id, limit_order] : orders_) {
    (void)order_id;
    if (limit_order.side == side) {
      out.push_back(limit_order);
    }
  }
  return out;
}

}
