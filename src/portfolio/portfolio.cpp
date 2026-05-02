#include "portfolio/portfolio.hpp"
#include "logging.hpp"

#include <algorithm>

namespace hft::portfolio {

namespace {

constexpr double FLOAT_EPSILON = 1e-12;

}

Portfolio::SPtr Portfolio::create(double initialCash) {
  return std::make_shared<Portfolio>(initialCash);
}

Portfolio::Portfolio(double initialCash)
    : initial_cash_(initialCash), cash_(initialCash) {}

void Portfolio::settleBuyAgainstLots(const Fill &fill, double &remaining) {
  while (remaining > FLOAT_EPSILON && !short_lots_.empty()) {
    auto &lot = short_lots_.front();
    const double matched_qty = std::min(remaining, lot.amount);
    realized_pnl_ += (lot.entry_price - fill.price) * matched_qty;
    lot.amount -= matched_qty;
    remaining -= matched_qty;
    if (lot.amount <= FLOAT_EPSILON) {
      short_lots_.pop_front();
    }
  }
  if (remaining > FLOAT_EPSILON) {
    long_lots_.push_back({fill.price, remaining});
  }
}

void Portfolio::settleSellAgainstLots(const Fill &fill, double &remaining) {
  while (remaining > FLOAT_EPSILON && !long_lots_.empty()) {
    auto &lot = long_lots_.front();
    const double matched_qty = std::min(remaining, lot.amount);
    realized_pnl_ += (fill.price - lot.entry_price) * matched_qty;
    lot.amount -= matched_qty;
    remaining -= matched_qty;
    if (lot.amount <= FLOAT_EPSILON) {
      long_lots_.pop_front();
    }
  }
  if (remaining > FLOAT_EPSILON) {
    short_lots_.push_back({fill.price, remaining});
  }
}

void Portfolio::applyFill(const Fill &fill) {
  if (fill.amount <= FLOAT_EPSILON) {
    return;
  }

  fills_.push_back(fill);
  turnover_ += fill.price * fill.amount;

  double remaining = fill.amount;

  if (fill.side == Side::Buy) {
    cash_ -= fill.price * fill.amount;
    settleBuyAgainstLots(fill, remaining);
    inventory_ += fill.amount;
  } else {
    cash_ += fill.price * fill.amount;
    settleSellAgainstLots(fill, remaining);
    inventory_ -= fill.amount;
  }

  logging::Logger::debug(
      "[PORTFOLIO] fill ", (fill.side == Side::Buy ? "BUY  " : "SELL "),
      " amount=", fill.amount, " price=", fill.price, " cash=", cash_,
      " inv=", inventory_, " realized=", realized_pnl_);
}

double Portfolio::unrealizedPnL(double midPrice) const noexcept {
  double pnl = 0.0;
  for (const auto &lot : long_lots_) {
    pnl += (midPrice - lot.entry_price) * lot.amount;
  }
  for (const auto &lot : short_lots_) {
    pnl += (lot.entry_price - midPrice) * lot.amount;
  }
  return pnl;
}

void Portfolio::mark(double midPrice, Timestamp ts) {
  equity_curve_.push_back(equity(midPrice));
  equity_ts_.push_back(ts);
}

}
