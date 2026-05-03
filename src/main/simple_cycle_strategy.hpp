#pragma once

#include <cstdint>
#include <string>

#include "strategy.hpp"

namespace hft {





class SimpleCycleStrategy final : public IStrategy {
   public:
    SimpleCycleStrategy(double order_qty, double take_profit_bps,
                        std::int64_t entry_refresh_us, std::int64_t max_position);

    std::string name() const override { return "SimpleCycleStrategy"; }

    StrategyActions on_book(const BookEvent& event, const ExchangeEmulator& exchange,
                            const StrategyContext& context) override;
    StrategyActions on_trade(const TradeEvent& event, const ExchangeEmulator& exchange,
                             const StrategyContext& context) override;
    void on_fill(const Fill& fill, const ExchangeEmulator& exchange,
                 const StrategyContext& context) override;

   private:

    enum class Mode { SeekingEntry, SeekingExit };

    double order_qty_ = 0.0;
    double take_profit_ratio_ = 0.0;
    std::int64_t entry_refresh_us_ = 0;
    std::int64_t max_position_ = 0;

    Mode mode_ = Mode::SeekingEntry;
    Timestamp last_quote_ts_ = 0;
    double last_entry_price_ = 0.0;
};

}
