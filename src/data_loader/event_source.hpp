#pragma once

#include "types.hpp"

#include <concepts>

namespace hft::data {

/**
 * @brief Describes sources that emit ordered market events.
 */
template <typename S>
concept EventSource = requires(S &source) {
  { source.hasNext() } -> std::convertible_to<bool>;
  { source.next() } -> std::same_as<MarketEvent>;
};

}
