#include "data_loader/csv_parsers.hpp"
#include "logging.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace hft::data {

namespace {

void rstrip(std::string &s) {
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n' ||
                        s.back() == ' ' || s.back() == '\t')) {
    s.pop_back();
  }
}

void toLower(std::string &s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
}

bool parseSide(const std::string &raw, Side &out) {
  std::string s = raw;
  rstrip(s);
  toLower(s);
  if (s == "buy") {
    out = Side::Buy;
    return true;
  }
  if (s == "sell") {
    out = Side::Sell;
    return true;
  }
  return false;
}

bool readDouble(std::stringstream &ss, double &out) {
  std::string cell;
  if (!std::getline(ss, cell, ',')) {
    return false;
  }
  rstrip(cell);
  if (cell.empty()) {
    out = 0.0;
    return true;
  }
  try {
    out = std::stod(cell);
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

bool parseLobTimestamp(std::stringstream &ss, LOBData &out) {
  std::string cell;
  if (!std::getline(ss, cell, ',')) {
    return false;
  }
  if (!std::getline(ss, cell, ',')) {
    return false;
  }

  rstrip(cell);
  try {
    out.ts = std::stoll(cell);
  } catch (const std::exception &) {
    logging::Logger::debug("[CSV] bad LOB ts cell='", cell, "'");
    return false;
  }
  return true;
}

void parseLobLevels(std::stringstream &ss, int maxDepth, LOBData &out) {
  out.asks.clear();
  out.bids.clear();
  out.asks.reserve(static_cast<std::size_t>(maxDepth));
  out.bids.reserve(static_cast<std::size_t>(maxDepth));

  for (int level = 0; level < maxDepth; ++level) {
    double ask_price = 0.0, ask_amount = 0.0, bid_price = 0.0, bid_amount = 0.0;
    if (!readDouble(ss, ask_price)) {
      break;
    }
    if (!readDouble(ss, ask_amount)) {
      break;
    }
    if (!readDouble(ss, bid_price)) {
      break;
    }
    if (!readDouble(ss, bid_amount)) {
      break;
    }
    if (!std::isfinite(ask_price) || !std::isfinite(ask_amount) ||
        !std::isfinite(bid_price) || !std::isfinite(bid_amount)) {
      break;
    }
    if (ask_price > 0.0 && ask_amount > 0.0) {
      out.asks.push_back({ask_price, ask_amount});
    }
    if (bid_price > 0.0 && bid_amount > 0.0) {
      out.bids.push_back({bid_price, bid_amount});
    }
  }

  std::sort(out.bids.begin(), out.bids.end(),
            [](const OrderBookEntry &a, const OrderBookEntry &b) {
              return a.price > b.price;
            });
  std::sort(out.asks.begin(), out.asks.end(),
            [](const OrderBookEntry &a, const OrderBookEntry &b) {
              return a.price < b.price;
            });
}

bool parseTradeTimestamp(std::stringstream &ss, TradeData &out) {
  std::string cell;
  if (!std::getline(ss, cell, ',')) {
    return false;
  }
  if (!std::getline(ss, cell, ',')) {
    return false;
  }

  rstrip(cell);
  try {
    out.ts = std::stoll(cell);
  } catch (const std::exception &) {
    logging::Logger::debug("[CSV] bad trade ts cell='", cell, "'");
    return false;
  }
  return true;
}

bool parseTradeBody(std::stringstream &ss, TradeData &out) {
  std::string cell;
  if (!std::getline(ss, cell, ',')) {
    return false;
  }
  if (!parseSide(cell, out.aggressor_side)) {
    logging::Logger::debug("[CSV] unknown side '", cell, "'");
    return false;
  }

  if (!readDouble(ss, out.price)) {
    return false;
  }
  if (!readDouble(ss, out.amount)) {
    return false;
  }
  if (!std::isfinite(out.price) || !std::isfinite(out.amount)) {
    return false;
  }
  if (out.price <= 0.0 || out.amount <= 0.0) {
    return false;
  }

  return true;
}

}

bool parseLobLine(const std::string &line, int maxDepth, LOBData &out) {
  if (line.empty()) {
    return false;
  }

  std::stringstream ss(line);
  if (!parseLobTimestamp(ss, out)) {
    return false;
  }

  parseLobLevels(ss, maxDepth, out);
  return !out.asks.empty() || !out.bids.empty();
}

bool parseTradeLine(const std::string &line, TradeData &out) {
  if (line.empty()) {
    return false;
  }

  std::stringstream ss(line);
  if (!parseTradeTimestamp(ss, out)) {
    return false;
  }
  if (!parseTradeBody(ss, out)) {
    return false;
  }

  return true;
}

}
