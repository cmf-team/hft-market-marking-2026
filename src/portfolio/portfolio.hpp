#pragma once

#include "types.hpp"

#include <deque>
#include <memory>
#include <vector>

namespace hft::portfolio {

/**
 * @brief Stores one open inventory lot.
 */
struct Lot {
  double entry_price{0.0};
  double amount{0.0};
};

/**
 * @brief Tracks cash, inventory, fills, lots, and equity during a backtest.
 */
class Portfolio {
public:
  using SPtr = std::shared_ptr<Portfolio>;

  /**
   * @brief Creates a shared portfolio instance.
   * @param initialCash Initial cash balance.
   * @return Shared pointer to the created portfolio.
   */
  static SPtr create(double initialCash = 0.0);

  /**
   * @brief Creates a portfolio with zero initial cash.
   */
  Portfolio() = default;

  /**
   * @brief Creates a portfolio with an initial cash balance.
   * @param initialCash Initial cash balance.
   */
  explicit Portfolio(double initialCash);

  /**
   * @brief Applies one fill to cash, inventory, PnL, and lot state.
   * @param fill Input execution fill.
   */
  void applyFill(const Fill &fill);

  /**
   * @brief Records mark-to-market equity at a timestamp.
   * @param midPrice Input mid price used for valuation.
   * @param ts Timestamp for the equity mark.
   */
  void mark(double midPrice, Timestamp ts);

  /**
   * @brief Returns the initial cash balance.
   * @return Initial cash balance.
   */
  double initialCash() const noexcept { return initial_cash_; }

  /**
   * @brief Returns the current cash balance.
   * @return Current cash balance.
   */
  double cash() const noexcept { return cash_; }

  /**
   * @brief Returns the current signed inventory.
   * @return Current signed inventory.
   */
  double inventory() const noexcept { return inventory_; }

  /**
   * @brief Returns realized profit and loss.
   * @return Realized profit and loss.
   */
  double realizedPnL() const noexcept { return realized_pnl_; }

  /**
   * @brief Returns gross traded notional.
   * @return Gross traded notional.
   */
  double turnover() const noexcept { return turnover_; }

  /**
   * @brief Calculates unrealized profit and loss at a mid price.
   * @param midPrice Input mid price used for valuation.
   * @return Unrealized profit and loss.
   */
  double unrealizedPnL(double midPrice) const noexcept;

  /**
   * @brief Calculates total profit and loss at a mid price.
   * @param midPrice Input mid price used for valuation.
   * @return Realized plus unrealized profit and loss.
   */
  double totalPnL(double midPrice) const noexcept {
    return realized_pnl_ + unrealizedPnL(midPrice);
  }

  /**
   * @brief Calculates portfolio equity at a mid price.
   * @param midPrice Input mid price used for valuation.
   * @return Cash plus marked inventory value.
   */
  double equity(double midPrice) const noexcept {
    return cash_ + inventory_ * midPrice;
  }

  /**
   * @brief Returns all recorded fills.
   * @return Reference to the fill history.
   */
  const std::vector<Fill> &fills() const noexcept { return fills_; }

  /**
   * @brief Returns the marked equity curve.
   * @return Reference to equity values.
   */
  const std::vector<double> &equityCurve() const noexcept {
    return equity_curve_;
  }

  /**
   * @brief Returns timestamps for the marked equity curve.
   * @return Reference to equity timestamps.
   */
  const std::vector<Timestamp> &equityCurveTs() const noexcept {
    return equity_ts_;
  }

  /**
   * @brief Returns open long inventory lots.
   * @return Reference to long lots.
   */
  const std::deque<Lot> &longLots() const noexcept { return long_lots_; }

  /**
   * @brief Returns open short inventory lots.
   * @return Reference to short lots.
   */
  const std::deque<Lot> &shortLots() const noexcept { return short_lots_; }

private:
  /**
   * @brief Settles a buy fill against short lots and remaining long inventory.
   * @param fill Input buy fill.
   * @param[in,out] remaining Remaining fill amount to settle.
   */
  void settleBuyAgainstLots(const Fill &fill, double &remaining);

  /**
   * @brief Settles a sell fill against long lots and remaining short inventory.
   * @param fill Input sell fill.
   * @param[in,out] remaining Remaining fill amount to settle.
   */
  void settleSellAgainstLots(const Fill &fill, double &remaining);

  double initial_cash_{0.0};
  double cash_{0.0};
  double inventory_{0.0};
  double realized_pnl_{0.0};
  double turnover_{0.0};

  std::deque<Lot> long_lots_;
  std::deque<Lot> short_lots_;

  std::vector<Fill> fills_;
  std::vector<double> equity_curve_;
  std::vector<Timestamp> equity_ts_;
};

}
