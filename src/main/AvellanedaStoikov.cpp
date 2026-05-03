#include "main/AvellanedaStoikov.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace cmf
{

AvellanedaStoikov::AvellanedaStoikov(const AvellanedaStoikovConfig& cfg)
    : cfg_(cfg), last_sigma_(cfg.sigma_override)
{
}

void AvellanedaStoikov::onTrade(const Trade&, Exchange&)
{
}

void AvellanedaStoikov::onFill(const Fill& fill)
{
    inventory_ += (fill.side == Side::Buy ? fill.quantity : -fill.quantity);
}

void AvellanedaStoikov::onLobUpdate(const LOBSnapshot& lob, Exchange& exchange)
{
    Price bid = lob.bids[0].price;
    Price ask = lob.asks[0].price;
    if (bid <= 0 || ask <= 0)
        return;

    Price mid = 0.5 * (bid + ask);
    updateSigma(lob.timestamp, mid);

    if (session_start_us_ == 0)
        session_start_us_ = lob.timestamp;

    if (cfg_.requote_us > 0 && last_quote_us_ != 0 &&
        lob.timestamp - last_quote_us_ < cfg_.requote_us)
        return;

    requote(lob, exchange);
    last_quote_us_ = lob.timestamp;
}

void AvellanedaStoikov::updateSigma(MicroTime ts, Price mid)
{
    if (cfg_.sigma_override > 0)
    {
        last_sigma_ = cfg_.sigma_override;
        return;
    }

    mid_history_.emplace_back(ts, mid);
    while (static_cast<int>(mid_history_.size()) > cfg_.sigma_window + 1)
        mid_history_.pop_front();

    if (static_cast<int>(mid_history_.size()) < 30)
        return;
    double sum = 0.0;
    int n = 0;
    for (std::size_t i = 1; i < mid_history_.size(); ++i)
    {
        Price ds = mid_history_[i].second - mid_history_[i - 1].second;
        double dt = (mid_history_[i].first - mid_history_[i - 1].first) / 1e6;
        if (dt <= 1e-9)
            continue;
        sum += (ds * ds) / dt;
        ++n;
    }
    if (n > 0)
        last_sigma_ = std::sqrt(sum / n);
}

void AvellanedaStoikov::requote(const LOBSnapshot& lob, Exchange& exchange)
{
    if (last_sigma_ <= 0)
        return;

    Price mid = 0.5 * (lob.bids[0].price + lob.asks[0].price);

    double tau;
    if (cfg_.fixed_horizon)
    {
        tau = cfg_.horizon_seconds;
    }
    else
    {
        double elapsed = (lob.timestamp - session_start_us_) / 1e6;
        if (elapsed >= cfg_.horizon_seconds)
        {
            session_start_us_ = lob.timestamp;
            elapsed = 0.0;
        }
        tau = cfg_.horizon_seconds - elapsed;
    }

    double gamma = cfg_.gamma;
    double k = cfg_.k;
    double sigma2 = last_sigma_ * last_sigma_;

    // Eq 29
    Price r = mid - inventory_ * gamma * sigma2 * tau;

    // Eq. 30
    Price spread = gamma * sigma2 * tau + (2.0 / gamma) * std::log(1.0 + gamma / k);
    Price half = 0.5 * spread;

    Price p_b = r - half;
    Price p_a = r + half;

    last_r_ = r;
    last_spread_ = spread;

    // Cancel any previous quotes before placing fresh ones
    exchange.cancelAll();

    bool can_buy = inventory_ + cfg_.order_qty <= cfg_.max_inventory;
    bool can_sell = inventory_ - cfg_.order_qty >= -cfg_.max_inventory;

    if (can_buy && p_b > 0)
        exchange.placeOrder(Side::Buy, OrderType::Limit, p_b, cfg_.order_qty);
    if (can_sell && p_a > 0)
        exchange.placeOrder(Side::Sell, OrderType::Limit, p_a, cfg_.order_qty);

    if (cfg_.verbose)
    {
        std::cout << "[AS] ts=" << lob.timestamp << " mid=" << mid << " q=" << inventory_
                  << " sigma=" << last_sigma_ << " tau=" << tau << " r=" << r
                  << " spread=" << spread << " bid=" << p_b << " ask=" << p_a << "\n";
    }
}

} // namespace cmf
