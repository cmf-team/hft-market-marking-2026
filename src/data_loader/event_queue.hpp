#pragma once

#include <concepts>
#include <variant>

namespace hft::data {

/**
 * @brief Marks that an input queue has reached the end of its stream.
 */
struct EndOfStream {};

template <typename T>
using QueueEvent = std::variant<T, EndOfStream>;

/**
 * @brief Describes queues that produce typed market data events.
 */
template <typename Q>
concept EventQueue = requires(Q &queue) {
  typename Q::value_type;
  { queue.tryPop() } -> std::same_as<QueueEvent<typename Q::value_type>>;
};

}
