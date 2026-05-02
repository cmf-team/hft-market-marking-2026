#pragma once

#include "common/BasicTypes.hpp"
#include "exec/matching_engine.hpp"
#include "strategy/strategy_base.hpp"

#include <cstddef>

namespace cmf
{

// Trivial market maker: on every book update, cancels prior quotes and
// re-quotes one bid at (best_bid + spread/2) and one ask at (best_ask - spread/2).
// Quote size is fixed (10,000). If a MatchingEngine pointer is set,
// quotes are forwarded to it on each book update.
class MarketMaker : public StrategyBase
{
  public:
    MarketMaker() = default;

    void set_matching_engine(MatchingEngine* me) noexcept { me_ = me; }

    void on_book_update(const OrderBook& book) override;

    Price bid_price() const noexcept { return bid_price_; }
    Price ask_price() const noexcept { return ask_price_; }
    Quantity bid_size() const noexcept { return bid_size_; }
    Quantity ask_size() const noexcept { return ask_size_; }
    bool active() const noexcept { return active_; }
    std::size_t requote_count() const noexcept { return requote_count_; }

  private:
    MatchingEngine* me_{nullptr};
    Price bid_price_{0.0};
    Price ask_price_{0.0};
    Quantity bid_size_{0.0};
    Quantity ask_size_{0.0};
    bool active_{false};
    std::size_t requote_count_{0};
};

} // namespace cmf
