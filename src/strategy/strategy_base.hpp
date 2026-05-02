#pragma once

#include "strategy/strategy_context.hpp"
#include "types.hpp"

#include <concepts>

namespace hft::strategy {

/**
 * @brief Describes strategy types that consume book snapshots.
 */
template <typename T>
concept Strategy =
    requires(T &strategy, const LOBData &book, StrategyContext &ctx) {
      { strategy.onMarketData(book, ctx) } -> std::same_as<void>;
    };

/**
 * @brief Calls a strategy start hook when the strategy defines it.
 * @param strategy Strategy instance to notify.
 * @param ctx Strategy context passed to the hook.
 */
template <typename T>
void callOnStart(T &strategy, StrategyContext &ctx) {
  if constexpr (requires { strategy.onStart(ctx); }) {
    strategy.onStart(ctx);
  }
}

/**
 * @brief Calls a strategy trade hook when the strategy defines it.
 * @param strategy Strategy instance to notify.
 * @param trade Trade print passed to the hook.
 * @param ctx Strategy context passed to the hook.
 */
template <typename T>
void callOnTrade(T &strategy, const TradeData &trade,
                 StrategyContext &ctx) {
  if constexpr (requires { strategy.onTrade(trade, ctx); }) {
    strategy.onTrade(trade, ctx);
  }
}

/**
 * @brief Calls a strategy fill hook when the strategy defines it.
 * @param strategy Strategy instance to notify.
 * @param fill Fill passed to the hook.
 * @param ctx Strategy context passed to the hook.
 */
template <typename T>
void callOnFill(T &strategy, const Fill &fill, StrategyContext &ctx) {
  if constexpr (requires { strategy.onFill(fill, ctx); }) {
    strategy.onFill(fill, ctx);
  }
}

/**
 * @brief Calls a strategy finish hook when the strategy defines it.
 * @param strategy Strategy instance to notify.
 * @param ctx Strategy context passed to the hook.
 */
template <typename T>
void callOnFinish(T &strategy, StrategyContext &ctx) {
  if constexpr (requires { strategy.onFinish(ctx); }) {
    strategy.onFinish(ctx);
  }
}

}
