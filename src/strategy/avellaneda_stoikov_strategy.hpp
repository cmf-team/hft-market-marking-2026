#pragma once

#include "strategy/strategy_context.hpp"
#include "types.hpp"

namespace hft::strategy {

/**
 * @brief Stores Avellaneda-Stoikov strategy parameters.
 */
struct AvellanedaStoikovParams {
  static constexpr double DEFAULT_GAMMA = 0.05;
  static constexpr double DEFAULT_SIGMA = 2e-6;
  static constexpr double DEFAULT_HORIZON_SEC = 60.0;
  static constexpr double DEFAULT_K = 430'000;
  static constexpr double DEFAULT_ORDER_QTY = 1.0;
  static constexpr double DEFAULT_TICK_SIZE = 1e-7;
  static constexpr double DEFAULT_MAX_SPREAD_TICKS = 8.0;
  static constexpr double DEFAULT_INVENTORY_LIMIT = 10'000.0;
  static constexpr double DEFAULT_VOLATILITY_EWMA_ALPHA = 0.02;

  double gamma{DEFAULT_GAMMA};
  double sigma{DEFAULT_SIGMA};
  double horizon_sec{DEFAULT_HORIZON_SEC};
  double k{DEFAULT_K};
  double order_qty{DEFAULT_ORDER_QTY};
  double tick_size{DEFAULT_TICK_SIZE};
  double min_spread_ticks{1.0};
  double max_spread_ticks{DEFAULT_MAX_SPREAD_TICKS};
  double inventory_limit{DEFAULT_INVENTORY_LIMIT};
  double volatility_ewma_alpha{DEFAULT_VOLATILITY_EWMA_ALPHA};
  Timestamp quote_interval_us{0};
  bool liquidate_on_finish{true};
};

/**
 * @brief Quotes bid and ask orders using an Avellaneda-Stoikov model.
 */
class AvellanedaStoikovStrategy {
public:
  /**
   * @brief Selects the fair price source used by the strategy.
   */
  enum class FairPrice {
    Mid,
    Microprice,
  };

  /**
   * @brief Creates a strategy with model parameters and fair price mode.
   * @param params Input strategy parameters.
   * @param fair_price Fair price source used for quoting.
   */
  explicit AvellanedaStoikovStrategy(
      const AvellanedaStoikovParams &params = AvellanedaStoikovParams{},
      FairPrice fair_price = FairPrice::Mid);

  /**
   * @brief Updates quotes from the latest book snapshot.
   * @param book Input book snapshot.
   * @param ctx Strategy context used for orders and portfolio state.
   */
  void onMarketData(const LOBData &book, StrategyContext &ctx);

  /**
   * @brief Handles the end of a backtest run.
   * @param ctx Strategy context used for optional liquidation.
   */
  void onFinish(StrategyContext &ctx);

private:
  /**
   * @brief Selects the current fair reference price.
   * @param ctx Strategy context providing market state.
   * @return Reference price used for quoting.
   */
  double referencePrice(const StrategyContext &ctx) const noexcept;

  /**
   * @brief Calculates the model spread.
   * @param ctx Strategy context providing market state.
   * @return Model spread in price units.
   */
  double modelSpread(const StrategyContext &ctx) const noexcept;

  /**
   * @brief Calculates the reservation price shift from inventory.
   * @param ctx Strategy context providing portfolio state.
   * @return Inventory-driven price shift.
   */
  double inventoryReservationShift(const StrategyContext &ctx) const noexcept;

  /**
   * @brief Rounds a bid price to the strategy tick grid.
   * @param price Raw bid price.
   * @return Rounded bid price.
   */
  double roundBid(double price) const noexcept;

  /**
   * @brief Rounds an ask price to the strategy tick grid.
   * @param price Raw ask price.
   * @return Rounded ask price.
   */
  double roundAsk(double price) const noexcept;

  AvellanedaStoikovParams params_;
  FairPrice fair_price_{FairPrice::Mid};
  double sigma2_{0.0};
  double last_mid_{0.0};
  Timestamp last_vol_ts_{0};
  Timestamp last_quote_ts_{0};
};

}
