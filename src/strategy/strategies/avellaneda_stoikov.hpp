#pragma once

#include "common/BasicTypes.hpp"
#include "exec/matching_engine.hpp"
#include "strategy/strategy_base.hpp"
#include "strategy/volatility.hpp"

#include <cstddef>

namespace cmf
{

struct AvellanedaStoikovParams
{
    double gamma{0.1};
    Price tick_size{0.01};
    Quantity q_max{100.0};
    Quantity q_min{-100.0};
    Quantity quote_size{10'000.0};
    int vol_window{100};
    double vol_dt{1.0};
    // Minimum half-spread enforced as a multiple of tick_size. When the AS
    // formula yields a sub-tick half-spread, both legs collapse onto adjacent
    // ticks straddling mid and tend to cross the book on the next update.
    // Floor it to keep the strategy posting a real spread.
    double min_half_spread_ticks{1.0};
};

// Avellaneda-Stoikov market maker (asymmetric-reservation form).
// Let factor = gamma * s^2 * (exp(sigma^2 * (T-t)) - 1). Quotes are
//   R^a = s + ((1 - 2q) / 2) * factor   -> ask
//   R^b = s + ((-1 - 2q) / 2) * factor  -> bid
// then tick-rounded: p_bid = floor(R^b / tick) * tick, p_ask = ceil(R^a / tick) * tick.
class AvellanedaStoikov : public StrategyBase
{
  public:
    using Params = AvellanedaStoikovParams;

    explicit AvellanedaStoikov(Params params = {});

    void set_matching_engine(MatchingEngine* me) noexcept { me_ = me; }
    void set_session_end(NanoTime T) noexcept { session_end_ = T; }

    void on_book_update(const OrderBook& book) override;
    void on_fill(const Fill& fill) override;

    Price bid_price() const noexcept { return bid_price_; }
    Price ask_price() const noexcept { return ask_price_; }
    Quantity inventory() const noexcept { return inventory_; }
    double reservation() const noexcept { return reservation_; }
    double half_spread() const noexcept { return half_spread_; }
    double sigma() const noexcept { return vol_.sigma(); }
    bool bid_active() const noexcept { return bid_active_; }
    bool ask_active() const noexcept { return ask_active_; }
    std::size_t requote_count() const noexcept { return requote_count_; }
    std::size_t cross_skips() const noexcept { return cross_skips_; }

  private:
    Params params_;
    Volatility vol_;
    MatchingEngine* me_{nullptr};

    NanoTime session_end_{static_cast<NanoTime>(1) << 62};

    Price prev_mid_{0.0};
    bool have_prev_mid_{false};

    Quantity inventory_{0.0};
    Price bid_price_{0.0};
    Price ask_price_{0.0};
    double reservation_{0.0};
    double half_spread_{0.0};
    bool bid_active_{false};
    bool ask_active_{false};
    std::size_t requote_count_{0};
    std::size_t cross_skips_{0};
};

} // namespace cmf
