#pragma once

#include "metrics/metric.hpp"

#include <string_view>

namespace hft::metrics::predefined {

/**
 * @brief Calculates realized profit and loss.
 */
class RealizedPnL final {
public:
  /**
   * @brief Returns the metric identifier.
   * @return Static metric name.
   */
  static constexpr std::string_view name() noexcept { return "realized_pnl"; }

  /**
   * @brief Calculates realized profit and loss for a portfolio.
   * @param portfolio Input portfolio state.
   * @return Realized profit and loss.
   */
  double calculate(const portfolio::Portfolio &portfolio) const;
};

/**
 * @brief Calculates total profit and loss.
 */
class TotalPnL final {
public:
  /**
   * @brief Returns the metric identifier.
   * @return Static metric name.
   */
  static constexpr std::string_view name() noexcept { return "total_pnl"; }

  /**
   * @brief Calculates total profit and loss for a portfolio.
   * @param portfolio Input portfolio state.
   * @return Total profit and loss.
   */
  double calculate(const portfolio::Portfolio &portfolio) const;
};

/**
 * @brief Calculates current inventory.
 */
class Inventory final {
public:
  /**
   * @brief Returns the metric identifier.
   * @return Static metric name.
   */
  static constexpr std::string_view name() noexcept { return "inventory"; }

  /**
   * @brief Calculates current inventory for a portfolio.
   * @param portfolio Input portfolio state.
   * @return Signed inventory amount.
   */
  double calculate(const portfolio::Portfolio &portfolio) const;
};

/**
 * @brief Calculates executed turnover.
 */
class Turnover final {
public:
  /**
   * @brief Returns the metric identifier.
   * @return Static metric name.
   */
  static constexpr std::string_view name() noexcept { return "turnover"; }

  /**
   * @brief Calculates executed turnover for a portfolio.
   * @param portfolio Input portfolio state.
   * @return Gross traded notional.
   */
  double calculate(const portfolio::Portfolio &portfolio) const;
};

/**
 * @brief Calculates the number of recorded fills.
 */
class FillCount final {
public:
  /**
   * @brief Returns the metric identifier.
   * @return Static metric name.
   */
  static constexpr std::string_view name() noexcept { return "fill_count"; }

  /**
   * @brief Calculates fill count for a portfolio.
   * @param portfolio Input portfolio state.
   * @return Number of fills as a floating-point value.
   */
  double calculate(const portfolio::Portfolio &portfolio) const;
};

/**
 * @brief Calculates maximum equity drawdown.
 */
class MaxDrawdown final {
public:
  /**
   * @brief Returns the metric identifier.
   * @return Static metric name.
   */
  static constexpr std::string_view name() noexcept { return "max_drawdown"; }

  /**
   * @brief Calculates maximum drawdown for a portfolio.
   * @param portfolio Input portfolio state.
   * @return Largest peak-to-trough equity decline.
   */
  double calculate(const portfolio::Portfolio &portfolio) const;
};

/**
 * @brief Calculates a simple per-step Sharpe ratio.
 */
class SharpeRatio final {
public:
  /**
   * @brief Returns the metric identifier.
   * @return Static metric name.
   */
  static constexpr std::string_view name() noexcept { return "sharpe_ratio"; }

  /**
   * @brief Calculates a per-step Sharpe ratio for a portfolio.
   * @param portfolio Input portfolio state.
   * @return Mean equity return divided by return standard deviation.
   */
  double calculate(const portfolio::Portfolio &portfolio) const;
};

}
