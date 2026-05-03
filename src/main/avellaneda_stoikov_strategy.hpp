#pragma once

#include <cstdint>
#include <string>

#include "strategy.hpp"

namespace hft {

struct AvellanedaStoikovParams {
    double order_qty = 1000.0;
    double max_position = 100000.0;

    double gamma = 0.0001;
    double k = 20000000.0;
    double sigma = 0.0;
    double sigma_floor = 0.00000005;
    double volatility_ewma_alpha = 0.05;

    std::int64_t horizon_us = 60000000;
    std::int64_t quote_refresh_us = 500000;

    double tick_size = 0.0000001;
    double min_spread_ticks = 1.0;
    double spread_multiplier = 1.0;

    bool use_microprice = false;
    double microprice_alpha = 1.0;
};

class AvellanedaStoikovStrategy final : public IStrategy {
   public:
    explicit AvellanedaStoikovStrategy(AvellanedaStoikovParams params);

    std::string name() const override;

    StrategyActions on_book(const BookEvent& event, const ExchangeEmulator& exchange,
                            const StrategyContext& context) override;
    StrategyActions on_trade(const TradeEvent& event, const ExchangeEmulator& exchange,
                             const StrategyContext& context) override;
    void on_fill(const Fill& fill, const ExchangeEmulator& exchange,
                 const StrategyContext& context) override;

   private:
    double fair_price(const BookEvent& event) const;
    void update_volatility(const BookEvent& event);
    double current_sigma() const;
    double model_spread(double sigma) const;
    double round_bid(double price) const;
    double round_ask(double price) const;

    AvellanedaStoikovParams params_;
    Timestamp last_quote_ts_ = 0;
    Timestamp last_mid_ts_ = 0;
    double last_mid_ = 0.0;
    double variance_rate_ = 0.0;
    bool has_mid_ = false;
};

}
