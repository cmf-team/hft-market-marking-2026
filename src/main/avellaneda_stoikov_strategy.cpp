#include "avellaneda_stoikov_strategy.hpp"

#include <algorithm>
#include <cmath>

namespace hft {

AvellanedaStoikovStrategy::AvellanedaStoikovStrategy(
    AvellanedaStoikovParams params)
    : params_(params) {}

std::string AvellanedaStoikovStrategy::name() const {
    return params_.use_microprice ? "MicropriceAvellanedaStoikovStrategy"
                                  : "AvellanedaStoikovStrategy";
}

double AvellanedaStoikovStrategy::fair_price(const BookEvent& event) const {
    const double mid = (event.best_bid + event.best_ask) * 0.5;
    if (!params_.use_microprice || event.best_bid_qty <= 0.0 ||
        event.best_ask_qty <= 0.0) {
        return mid;
    }

    const double total_qty = event.best_bid_qty + event.best_ask_qty;
    if (total_qty <= 0.0) {
        return mid;
    }

    const double imbalance = event.best_bid_qty / total_qty;
    const double spread = event.best_ask - event.best_bid;
    const double adjustment =
        params_.microprice_alpha * (imbalance - 0.5) * spread;

    return std::clamp(mid + adjustment, event.best_bid, event.best_ask);
}

void AvellanedaStoikovStrategy::update_volatility(const BookEvent& event) {
    const double mid = (event.best_bid + event.best_ask) * 0.5;
    if (mid <= 0.0) {
        return;
    }

    if (has_mid_ && event.ts > last_mid_ts_) {
        const double dt_seconds =
            static_cast<double>(event.ts - last_mid_ts_) / 1000000.0;
        if (dt_seconds > 0.0) {
            const double diff = mid - last_mid_;
            const double instant_variance_rate = (diff * diff) / dt_seconds;
            const double alpha =
                std::clamp(params_.volatility_ewma_alpha, 0.0, 1.0);
            if (variance_rate_ <= 0.0) {
                variance_rate_ = instant_variance_rate;
            } else {
                variance_rate_ =
                    (1.0 - alpha) * variance_rate_ +
                    alpha * instant_variance_rate;
            }
        }
    }

    last_mid_ = mid;
    last_mid_ts_ = event.ts;
    has_mid_ = true;
}

double AvellanedaStoikovStrategy::current_sigma() const {
    if (params_.sigma > 0.0) {
        return params_.sigma;
    }

    const double floor = std::max(0.0, params_.sigma_floor);
    return std::max(std::sqrt(std::max(0.0, variance_rate_)), floor);
}

double AvellanedaStoikovStrategy::model_spread(double sigma) const {
    const double tau =
        std::max<std::int64_t>(params_.horizon_us, 0) / 1000000.0;
    const double risk_spread = params_.gamma * sigma * sigma * tau;

    if (params_.k <= 0.0) {
        return risk_spread;
    }

    if (params_.gamma <= 0.0) {
        return risk_spread + 2.0 / params_.k;
    }

    return risk_spread +
           (2.0 / params_.gamma) * std::log1p(params_.gamma / params_.k);
}

double AvellanedaStoikovStrategy::round_bid(double price) const {
    if (params_.tick_size <= 0.0) {
        return price;
    }
    return std::floor(price / params_.tick_size) * params_.tick_size;
}

double AvellanedaStoikovStrategy::round_ask(double price) const {
    if (params_.tick_size <= 0.0) {
        return price;
    }
    return std::ceil(price / params_.tick_size) * params_.tick_size;
}

StrategyActions AvellanedaStoikovStrategy::on_book(
    const BookEvent& event, const ExchangeEmulator& exchange,
    const StrategyContext& context) {
    (void)exchange;

    StrategyActions actions;
    if (event.best_bid <= 0.0 || event.best_ask <= 0.0 ||
        event.best_bid >= event.best_ask) {
        return actions;
    }

    update_volatility(event);

    const bool should_requote =
        last_quote_ts_ == 0 || params_.quote_refresh_us <= 0 ||
        (event.ts - last_quote_ts_) >= params_.quote_refresh_us;
    if (!should_requote) {
        return actions;
    }

    const double sigma = current_sigma();
    const double tau =
        std::max<std::int64_t>(params_.horizon_us, 0) / 1000000.0;
    const double inventory_skew = context.position * params_.gamma * sigma *
                                  sigma * tau;
    const double reservation_price = fair_price(event) - inventory_skew;

    const double observed_spread = event.best_ask - event.best_bid;
    const double min_configured_spread =
        std::max(0.0, params_.min_spread_ticks) *
        std::max(0.0, params_.tick_size);
    const double min_spread =
        std::max(min_configured_spread,
                 observed_spread * std::max(0.0, params_.spread_multiplier));

    const double spread = std::max(model_spread(sigma), min_spread);
    const double half_spread = spread * 0.5;

    double bid_price =
        round_bid(std::min(reservation_price - half_spread, event.best_bid));
    double ask_price =
        round_ask(std::max(reservation_price + half_spread, event.best_ask));

    if (bid_price <= 0.0 || ask_price <= 0.0 || bid_price >= ask_price) {
        return actions;
    }

    const double max_position = std::max(0.0, params_.max_position);
    const double buy_qty =
        std::min(params_.order_qty, std::max(0.0, max_position - context.position));
    const double sell_qty =
        std::min(params_.order_qty, std::max(0.0, max_position + context.position));

    actions.cancel_all = true;

    if (buy_qty > 0.0) {
        OrderRequest order;
        order.side = Side::Buy;
        order.type = OrderType::Limit;
        order.qty = buy_qty;
        order.price = bid_price;
        actions.new_orders.push_back(order);
    }

    if (sell_qty > 0.0) {
        OrderRequest order;
        order.side = Side::Sell;
        order.type = OrderType::Limit;
        order.qty = sell_qty;
        order.price = ask_price;
        actions.new_orders.push_back(order);
    }

    last_quote_ts_ = event.ts;
    return actions;
}

StrategyActions AvellanedaStoikovStrategy::on_trade(
    const TradeEvent& event, const ExchangeEmulator& exchange,
    const StrategyContext& context) {
    (void)event;
    (void)exchange;
    (void)context;
    return {};
}

void AvellanedaStoikovStrategy::on_fill(const Fill& fill,
                                        const ExchangeEmulator& exchange,
                                        const StrategyContext& context) {
    (void)fill;
    (void)exchange;
    (void)context;
    last_quote_ts_ = 0;
}

}
