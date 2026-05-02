#include "strategy/avellaneda_stoikov_strategy.hpp"

#include <algorithm>
#include <cmath>

namespace hft::strategy {

namespace {

constexpr double FLOAT_EPSILON = 1e-12;
constexpr double MICROSECONDS_PER_SECOND = 1'000'000.0;

}

AvellanedaStoikovStrategy::AvellanedaStoikovStrategy(
    const AvellanedaStoikovParams &params, const FairPrice fair_price)
    : params_(params), fair_price_(fair_price),
      sigma2_(params.sigma * params.sigma) {}

void AvellanedaStoikovStrategy::onMarketData(const LOBData &,
                                             StrategyContext &ctx) {

  if (!ctx.hasBook() || ctx.bestBid() <= 0.0 || ctx.bestAsk() <= 0.0) {
    return;
  }

  if (params_.quote_interval_us > 0 && last_quote_ts_ > 0 &&
      ctx.now() - last_quote_ts_ < params_.quote_interval_us) {
    return;
  }
  last_quote_ts_ = ctx.now();

  ctx.cancelAll();

  const double ref = referencePrice(ctx);
  if (ref <= 0.0) {
    return;
  }

  const double spread = modelSpread(ctx);
  const double reservation = ref - inventoryReservationShift(ctx);
  double bid = roundBid(reservation - spread * 0.5);
  double ask = roundAsk(reservation + spread * 0.5);

  bid = std::min(bid, ctx.bestBid());
  ask = std::max(ask, ctx.bestAsk());

  const double inventory = ctx.inventory();
  if (bid > 0.0 && inventory < params_.inventory_limit) {
    const double qty =
        std::min(params_.order_qty, params_.inventory_limit - inventory);
    if (qty > FLOAT_EPSILON) {
      ctx.placeLimit(Side::Buy, bid, qty);
    }
  }

  if (ask > 0.0 && inventory > -params_.inventory_limit) {
    const double qty =
        std::min(params_.order_qty, inventory + params_.inventory_limit);
    if (qty > FLOAT_EPSILON) {
      ctx.placeLimit(Side::Sell, ask, qty);
    }
  }
}

void AvellanedaStoikovStrategy::onFinish(StrategyContext &ctx) {
  ctx.cancelAll();
  if (!params_.liquidate_on_finish || !ctx.hasBook()) {
    return;
  }

  const double inventory = ctx.inventory();
  if (inventory > FLOAT_EPSILON) {
    ctx.executeMarket(Side::Sell, inventory);
  } else if (inventory < -FLOAT_EPSILON) {
    ctx.executeMarket(Side::Buy, -inventory);
  }
}

double AvellanedaStoikovStrategy::referencePrice(
    const StrategyContext &ctx) const noexcept {
  const double mid = ctx.mid();
  if (fair_price_ == FairPrice::Mid) {
    return mid;
  }

  return ctx.microprice();
}

double AvellanedaStoikovStrategy::modelSpread(
    const StrategyContext &ctx) const noexcept {
  const double gamma = params_.gamma;
  const double k = params_.k;
  const double tau = std::max(params_.horizon_sec, 0.0);

  const double risk_spread = gamma * sigma2_ * tau;
  const double inv_gamma = 1.0 / gamma;
  const double intensity_spread =
      (inv_gamma + inv_gamma) * std::log1p(gamma / k);
  const double raw_spread = risk_spread + intensity_spread;

  const double tick = params_.tick_size;
  const double external_spread = std::max(ctx.spread(), tick);
  const double min_spread =
      std::max(external_spread, params_.min_spread_ticks * tick);
  double max_spread = params_.max_spread_ticks > 0.0
                          ? params_.max_spread_ticks * tick
                          : min_spread;
  max_spread = std::max(max_spread, min_spread);

  return std::clamp(raw_spread, min_spread, max_spread);
}

double AvellanedaStoikovStrategy::inventoryReservationShift(
    const StrategyContext &ctx) const noexcept {
  return ctx.inventory() * params_.gamma * sigma2_ *
         std::max(params_.horizon_sec, 0.0);
}

double AvellanedaStoikovStrategy::roundBid(double price) const noexcept {
  const double tick = params_.tick_size;
  return std::floor(price / tick) * tick;
}

double AvellanedaStoikovStrategy::roundAsk(double price) const noexcept {
  const double tick = params_.tick_size;
  return std::ceil(price / tick) * tick;
}

}
