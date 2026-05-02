#pragma once

#include <concepts>
#include <string_view>

namespace hft::portfolio {
class Portfolio;
}

namespace hft::metrics {

/**
 * @brief Describes compile-time portfolio metric types.
 */
template <typename T>
concept Metric =
    requires(const T &metric, const portfolio::Portfolio &portfolio) {
  { T::name() } -> std::convertible_to<std::string_view>;
  { metric.calculate(portfolio) } -> std::convertible_to<double>;
};

}
