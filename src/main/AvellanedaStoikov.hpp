#pragma once

#include "common/Strategy.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <utility>

namespace cmf
{

struct AvellanedaStoikovConfig
{
    double gamma = 0.5;             // risk aversion γ
    double k = 1.5;                 // order-arrival decay parameter k
    double horizon_seconds = 600.0; // session length (or fixed (T−t)) in seconds
    bool fixed_horizon = false;     // if true, use τ ≡ horizon_seconds (stationary form)
    Quantity order_qty = 1.0;       // size of each posted limit
    Quantity max_inventory = 50.0;  // suppress side that would breach |q| > max
    int sigma_window = 500;         // rolling window for σ estimation
    double sigma_override = 0.0;    // if > 0, skip estimation and use this σ
    int requote_us = 0;             // requote no more often than this (0 = every LOB update)
    bool verbose = false;
};

class AvellanedaStoikov : public Strategy
{
  public:
    explicit AvellanedaStoikov(const AvellanedaStoikovConfig& cfg);

    void onTrade(const Trade& trade, Exchange& exchange) override;
    void onLobUpdate(const LOBSnapshot& lob, Exchange& exchange) override;
    void onFill(const Fill& fill) override;

    std::string name() const override { return "avellaneda_stoikov"; }

    Quantity inventory() const { return inventory_; }
    double lastSigma() const { return last_sigma_; }
    Price lastReservation() const { return last_r_; }
    Price lastSpread() const { return last_spread_; }

  private:
    AvellanedaStoikovConfig cfg_;

    Quantity inventory_ = 0.0;

    std::deque<std::pair<MicroTime, Price>> mid_history_;
    double last_sigma_ = 0.0;

    MicroTime session_start_us_ = 0;
    MicroTime last_quote_us_ = 0;

    Price last_r_ = 0.0;
    Price last_spread_ = 0.0;

    void updateSigma(MicroTime ts, Price mid);
    void requote(const LOBSnapshot& lob, Exchange& exchange);
};

} // namespace cmf
