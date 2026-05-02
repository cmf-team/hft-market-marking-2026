#include "strategy/strategy_context.hpp"

#include <cmath>

namespace hft::strategy {

namespace {

constexpr double PRICE_COMPARE_EPSILON = 1e-12;

}

StrategyContext::StrategyContext(execution::MatchingEngine *engine,
                                 portfolio::Portfolio *portfolio) noexcept
    : engine_(engine), portfolio_(portfolio) {}

void StrategyContext::setFillSink(FillSink sink) {
  fill_sink_ = std::move(sink);
}

void StrategyContext::setBook(const LOBData &book) noexcept {
  book_ = &book;
}

void StrategyContext::setNow(Timestamp ts) noexcept { now_ = ts; }

OrderId StrategyContext::placeLimit(const Side side, const double price,
                                    const double amount) {
  return engine_->placeLimit(side, price, amount, now_,
                             queueAheadAt(side, price));
}

std::vector<Fill> StrategyContext::executeMarket(Side side, double amount) {
  if (!book_)
    return {};
  std::vector<Fill> fills =
      engine_->executeMarket(side, amount, *book_, now_);
  if (fill_sink_) {
    for (const auto &fill : fills)
      fill_sink_(fill, *this);
  }
  return fills;
}

bool StrategyContext::cancel(const OrderId id) { return engine_->cancel(id); }

void StrategyContext::cancelAll() { engine_->cancelAll(); }

void StrategyContext::cancelSide(const Side side) {
  engine_->cancelSide(side);
}

std::vector<LimitOrder> StrategyContext::activeOrders() const {
  return engine_->activeOrders();
}

std::vector<LimitOrder> StrategyContext::activeOrders(const Side side) const {
  return engine_->activeOrders(side);
}

std::size_t StrategyContext::activeCount() const {
  return engine_->activeCount();
}

double StrategyContext::totalPnL() const noexcept {
  if (!book_ || book_->bids.empty() || book_->asks.empty())
    return portfolio_->realizedPnL();
  const double m = mid();
  if (m <= 0.0)
    return portfolio_->realizedPnL();
  return portfolio_->totalPnL(m);
}

double StrategyContext::bestBid() const noexcept {
  if (!book_ || book_->bids.empty())
    return 0.0;
  return book_->bids.front().price;
}

double StrategyContext::bestAsk() const noexcept {
  if (!book_ || book_->asks.empty())
    return 0.0;
  return book_->asks.front().price;
}

double StrategyContext::bidSize() const noexcept {
  if (!book_ || book_->bids.empty())
    return 0.0;
  return book_->bids.front().amount;
}

double StrategyContext::askSize() const noexcept {
  if (!book_ || book_->asks.empty())
    return 0.0;
  return book_->asks.front().amount;
}

double StrategyContext::mid() const noexcept {
  if (!book_ || book_->bids.empty() || book_->asks.empty())
    return 0.0;
  return 0.5 * (book_->bids.front().price + book_->asks.front().price);
}

double StrategyContext::microprice() const noexcept {
  if (!book_ || book_->bids.empty() || book_->asks.empty())
    return mid();
  const double bp = book_->bids.front().price;
  const double bq = book_->bids.front().amount;
  const double ap = book_->asks.front().price;
  const double aq = book_->asks.front().amount;
  const double denom = bq + aq;
  if (denom <= 0.0)
    return 0.5 * (bp + ap);
  return (ap * bq + bp * aq) / denom;
}

double StrategyContext::spread() const noexcept {
  if (!book_ || book_->bids.empty() || book_->asks.empty())
    return 0.0;
  return book_->asks.front().price - book_->bids.front().price;
}

double StrategyContext::queueAheadAt(Side side, double price) const noexcept {
  if (!book_ || price <= 0.0)
    return 0.0;

  const auto &levels = side == Side::Buy ? book_->bids : book_->asks;
  for (const auto &level : levels) {
    if (std::abs(level.price - price) <= PRICE_COMPARE_EPSILON)
      return level.amount;
  }
  return 0.0;
}

}
