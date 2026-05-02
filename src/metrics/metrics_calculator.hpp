#pragma once

#include "metrics/metric.hpp"
#include "metrics/predefined_metrics.hpp"

#include <cmath>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hft::portfolio {
class Portfolio;
}

namespace hft::metrics {

/**
 * @brief Calculates a fixed set of portfolio metrics without runtime dispatch.
 */
template <Metric... Metrics> class MetricsCalculatorT {
public:
  /**
   * @brief Creates a calculator with default-constructed metrics.
   */
  MetricsCalculatorT() = default;

  /**
   * @brief Creates a calculator from explicit metric instances.
   * @param metrics Metric instances stored by the calculator.
   */
  explicit MetricsCalculatorT(Metrics... metrics)
      : metrics_(std::move(metrics)...) {}

  /**
   * @brief Calculates all registered metrics for a portfolio.
   * @param portfolio Input portfolio state.
   * @return Map from metric name to calculated value.
   */
  std::unordered_map<std::string, double>
  calculateAll(const portfolio::Portfolio &portfolio) const {
    std::unordered_map<std::string, double> out;
    out.reserve(sizeof...(Metrics));
    std::apply(
        [&](const auto &...metric) {
          (addMetricResult(out, metric, portfolio), ...);
        },
        metrics_);
    return out;
  }

  /**
   * @brief Calculates one registered metric by name.
   * @param name Input metric name.
   * @param portfolio Input portfolio state.
   * @return Metric value, or NaN when the name is unknown.
   */
  double calculate(const std::string &name,
                   const portfolio::Portfolio &portfolio) const {
    double result = std::nan("");
    std::apply(
        [&](const auto &...metric) {
          (tryCalculate(name, result, metric, portfolio), ...);
        },
        metrics_);
    return result;
  }

  /**
   * @brief Returns all registered metric names.
   * @return Vector of metric names.
   */
  std::vector<std::string> availableMetrics() const {
    std::vector<std::string> names;
    names.reserve(sizeof...(Metrics));
    std::apply(
        [&](const auto &...metric) { (addMetricName(names, metric), ...); },
        metrics_);
    return names;
  }

private:
  /**
   * @brief Adds one metric calculation result to an output map.
   * @param[out] out Output map receiving the metric result.
   * @param metric Metric instance used for calculation.
   * @param portfolio Input portfolio state.
   */
  template <Metric MetricT>
  static void addMetricResult(std::unordered_map<std::string, double> &out,
                              const MetricT &metric,
                              const portfolio::Portfolio &portfolio) {
    out.emplace(std::string(MetricT::name()), metric.calculate(portfolio));
  }

  /**
   * @brief Calculates one metric when its name matches the requested name.
   * @param name Input requested metric name.
   * @param[in,out] result Output value updated on a name match.
   * @param metric Metric instance used for calculation.
   * @param portfolio Input portfolio state.
   */
  template <Metric MetricT>
  static void tryCalculate(const std::string &name, double &result,
                           const MetricT &metric,
                           const portfolio::Portfolio &portfolio) {
    if (std::string_view(name) == MetricT::name()) {
      result = metric.calculate(portfolio);
    }
  }

  /**
   * @brief Appends one metric name to a name list.
   * @param[out] names Output vector receiving the metric name.
   * @param metric Metric instance whose type provides the name.
   */
  template <Metric MetricT>
  static void addMetricName(std::vector<std::string> &names,
                            const MetricT &metric) {
    (void)metric;
    names.emplace_back(MetricT::name());
  }

  std::tuple<Metrics...> metrics_;
};

using MetricsCalculator =
    MetricsCalculatorT<predefined::RealizedPnL, predefined::TotalPnL,
                       predefined::Inventory, predefined::Turnover,
                       predefined::FillCount, predefined::MaxDrawdown,
                       predefined::SharpeRatio>;

}
