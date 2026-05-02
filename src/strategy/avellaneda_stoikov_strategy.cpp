#include "strategy/avellaneda_stoikov_strategy.hpp"
#include <algorithm>
#include <cmath>

namespace hft_backtest {

namespace {

// Привести "double-цену" к ближайшему шагу в "центах" с округлением вниз.
inline Price floor_to_tick(double price_cents, Price tick) {
    if (tick == 0) return static_cast<Price>(std::max(0.0, price_cents));
    auto v = static_cast<int64_t>(std::floor(price_cents / static_cast<double>(tick)));
    if (v < 0) v = 0;
    return static_cast<Price>(v) * tick;
}

inline Price ceil_to_tick(double price_cents, Price tick) {
    if (tick == 0) return static_cast<Price>(std::max(0.0, price_cents));
    auto v = static_cast<int64_t>(std::ceil(price_cents / static_cast<double>(tick)));
    if (v < 0) v = 0;
    return static_cast<Price>(v) * tick;
}

}  // namespace

AvellanedaStoikovStrategy::AvellanedaStoikovStrategy(const AvellanedaStoikovConfig& cfg)
    : cfg_(cfg), cash_(cfg.initial_cash) {}

std::string AvellanedaStoikovStrategy::name() const {
    return "AvellanedaStoikov-2008";
}

double AvellanedaStoikovStrategy::reference_price(const OrderBookSnapshot& snap) const {
    if (snap.bids.empty() || snap.asks.empty()) return 0.0;
    return 0.5 * (static_cast<double>(snap.bids.front().first) +
                  static_cast<double>(snap.asks.front().first));
}

void AvellanedaStoikovStrategy::update_sigma(double mid) {
    // Avellaneda-Stoikov требует ABSOLUTE volatility mid (в тех же ценовых
    // единицах, что и reference price s). Поэтому считаем sigma как stddev
    // приращений (mid_t - mid_{t-1}), а НЕ log-returns.
    if (last_mid_ > 0.0 && mid > 0.0) {
        const double diff = mid - last_mid_;
        log_returns_.push_back(diff);
        if (log_returns_.size() > cfg_.sigma_window) {
            log_returns_.pop_front();
        }
        if (log_returns_.size() >= 2) {
            double sum = 0.0, sum2 = 0.0;
            for (double x : log_returns_) { sum += x; sum2 += x * x; }
            const double n = static_cast<double>(log_returns_.size());
            const double mean = sum / n;
            const double var  = std::max(0.0, sum2 / n - mean * mean);
            last_sigma_ = std::sqrt(var);
        }
    }
    last_mid_ = mid;
}

StrategyAction AvellanedaStoikovStrategy::on_market_data(const OrderBookSnapshot& snapshot,
                                                        uint64_t timestamp_us) {
    StrategyAction action;
    action.cancels.push_back(CancelRequest{true, 0});

    if (snapshot.bids.empty() || snapshot.asks.empty()) return action;
    if (start_ts_us_ == 0) start_ts_us_ = timestamp_us;

    const double s = reference_price(snapshot);
    last_reference_ = s;

    const double mid = 0.5 * (static_cast<double>(snapshot.bids.front().first) +
                              static_cast<double>(snapshot.asks.front().first));
    update_sigma(mid);
    if (last_sigma_ <= 0.0) return action;

    const double elapsed_s = static_cast<double>(timestamp_us - start_ts_us_) * 1e-6;
    const double T = std::max(1.0, cfg_.T_seconds);
    double tau = 1.0 - std::min(1.0, elapsed_s / T);
    if (tau < 1e-3) tau = 1e-3;

    const double q     = cfg_.enable_inventory_skew ? inventory_ : 0.0;
    const double sigma = last_sigma_;
    const double gamma = cfg_.gamma;
    const double k     = cfg_.k;

    const double r = s - q * gamma * sigma * sigma * tau;
    const double half_spread =
        0.5 * gamma * sigma * sigma * tau + (1.0 / gamma) * std::log1p(gamma / k);
    last_reservation_ = r;
    last_half_spread_ = half_spread;

    const double bid_target = r - half_spread;
    const double ask_target = r + half_spread;

    Price bid_px = floor_to_tick(bid_target, cfg_.tick_size_cents);
    Price ask_px = ceil_to_tick(ask_target,  cfg_.tick_size_cents);

    if (bid_px >= ask_px && cfg_.tick_size_cents > 0) {
        ask_px = bid_px + cfg_.tick_size_cents;
    }

    const Quantity qty = static_cast<Quantity>(std::max(1.0, cfg_.order_size));

    if (inventory_ < cfg_.max_inventory && bid_px > 0) {
        action.quotes.push_back(QuoteRequest{Side::BUY, bid_px, qty});
    }
    if (inventory_ > -cfg_.max_inventory && ask_px > 0) {
        action.quotes.push_back(QuoteRequest{Side::SELL, ask_px, qty});
    }
    return action;
}

void AvellanedaStoikovStrategy::on_fill(const FillReport& fill) {
    const double price_units = static_cast<double>(fill.price) / 10000.0;
    const double qty         = static_cast<double>(fill.quantity);
    if (fill.side == Side::BUY) {
        inventory_ += qty;
        cash_      -= price_units * qty;
    } else {
        inventory_ -= qty;
        cash_      += price_units * qty;
    }
}

std::string AvellanedaStoikovMicroStrategy::name() const {
    return "AvellanedaStoikov-2018-Microprice";
}

double AvellanedaStoikovMicroStrategy::reference_price(const OrderBookSnapshot& snap) const {
    if (snap.bids.empty() || snap.asks.empty()) return 0.0;
    const double bid_px = static_cast<double>(snap.bids.front().first);
    const double ask_px = static_cast<double>(snap.asks.front().first);
    const double bid_q  = static_cast<double>(snap.bids.front().second);
    const double ask_q  = static_cast<double>(snap.asks.front().second);
    const double w_sum  = bid_q + ask_q;
    if (w_sum <= 0.0) return 0.5 * (bid_px + ask_px);
    // Microprice -- средневзвешенная по противоположному размеру.
    return (bid_px * ask_q + ask_px * bid_q) / w_sum;
}

}  // namespace hft_backtest
