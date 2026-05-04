#pragma once
#include "backtest/order_book/order_book.hpp"
#include "common/BasicTypes.hpp"
#include <algorithm>
#include <limits>

// ---------------------------------------------------------------------------
// TrendFollowingStrategy — dual-EMA crossover on mid_ticks(), gated by OBI
//
//  Signal rules (edge-triggered):
//    fast_ema crosses above slow_ema  AND  OBI > +obi_threshold  → go Long
//    fast_ema crosses below slow_ema  AND  OBI < -obi_threshold  → go Short
//    opposite cross                                               → flatten
//
//  Execution:
//    Cancel all open orders, then place one aggressive limit order
//    (buy at best_ask / sell at best_bid) sized to reach ±max_qty.
//    placement_ts = lob.timestamp  ⟹ T+1 fill semantics.
//
//  Parameters:
//    max_qty       – maximum absolute position in raw units
//    fast_period   – EMA period for fast line (default 12)
//    slow_period   – EMA period for slow line (default 26)
//    obi_depth     – number of LOB levels used for OBI (default 5)
//    obi_threshold – min |OBI ratio| in [0,1] required to enter (default 0.1)
//    warmup_ticks  – LOB snapshots consumed before signals fire (default 50)
// ---------------------------------------------------------------------------
class TrendFollowingStrategy
{
  public:
    explicit TrendFollowingStrategy(int64_t max_qty,
                                    uint32_t fast_period = 500,
                                    uint32_t slow_period = 2000,
                                    int32_t obi_depth = 5,
                                    double obi_threshold = 0.1,
                                    uint32_t warmup_ticks = 2500,
                                    uint32_t min_ticks_between_flips = 100,
                                    double hysteresis_ticks = 0.0)
        : max_qty_(max_qty), alpha_fast_(2.0 / (fast_period + 1)), alpha_slow_(2.0 / (slow_period + 1)), obi_depth_(std::min(obi_depth, LOB_DEPTH)), obi_threshold_(obi_threshold), warmup_ticks_(warmup_ticks), min_ticks_between_flips_(min_ticks_between_flips), hysteresis_ticks_(hysteresis_ticks)
    {
    }

    [[nodiscard]] static uint64_t trade_rows() noexcept { return 0; }
    [[nodiscard]] static std::string_view name() noexcept { return "trend_ema"; }

    template <OrderBookLike OrderBookT = OrderBook>
    void on_lob(const LobSnapshot& lob, OrderBookT& ob, PnlState& pnl) noexcept
    {
        if (ticks_since_last_flip_ != std::numeric_limits<std::uint32_t>::max())
            ++ticks_since_last_flip_;

        const double mid = 0.5 * static_cast<double>(lob.asks[0].price + lob.bids[0].price);

        // ── Warm-up: seed EMAs, no trading ───────────────────────────────
        if (ticks_seen_ < warmup_ticks_)
        {
            fast_ema_ = (ticks_seen_ == 0) ? mid : fast_ema_ + alpha_fast_ * (mid - fast_ema_);
            slow_ema_ = (ticks_seen_ == 0) ? mid : slow_ema_ + alpha_slow_ * (mid - slow_ema_);
            ++ticks_seen_;
            return;
        }

        // ── Update EMAs ───────────────────────────────────────────────────
        const double prev_fast = fast_ema_;
        const double prev_slow = slow_ema_;
        fast_ema_ += alpha_fast_ * (mid - fast_ema_);
        slow_ema_ += alpha_slow_ * (mid - slow_ema_);

        // ── Detect crossover (edge trigger with hysteresis) ────────────────
        const double prev_spread = prev_fast - prev_slow;
        const double curr_spread = fast_ema_ - slow_ema_;
        const bool was_long = (prev_spread > hysteresis_ticks_);
        const bool is_long = (curr_spread > hysteresis_ticks_);
        const bool was_short = (prev_spread < -hysteresis_ticks_);
        const bool is_short = (curr_spread < -hysteresis_ticks_);

        const bool bull_cross = (!was_long && is_long);   // golden cross
        const bool bear_cross = (!was_short && is_short); // death cross

        if (!bull_cross && !bear_cross)
            return; // no new signal

        // ── Cancel all open orders on every detected cross ─────────────────
        ob.drain(Side::Buy);
        ob.drain(Side::Sell);

        // ── OBI confirmation: ratio = (bid_vol - ask_vol) / total_vol ────
        //    range [-1, +1]; positive → buy pressure; negative → sell pressure
        int64_t bid_vol = 0, ask_vol = 0;
        for (int32_t i = 0; i < obi_depth_; ++i)
        {
            bid_vol += lob.bids[i].amount;
            ask_vol += lob.asks[i].amount;
        }
        const int64_t total_vol = bid_vol + ask_vol;
        const double obi_ratio = (total_vol > 0)
                                     ? static_cast<double>(bid_vol - ask_vol) / static_cast<double>(total_vol)
                                     : 0.0;

        const bool obi_confirms_long = (obi_ratio > obi_threshold_);
        const bool obi_confirms_short = (obi_ratio < -obi_threshold_);

        // ── Determine target position ─────────────────────────────────────
        //    On a bull cross: if OBI confirms, go long; else at least exit short.
        //    On a bear cross: if OBI confirms, go short; else at least exit long.
        int64_t target = pnl.position; // default: hold
        if (bull_cross)
        {
            target = obi_confirms_long ? +max_qty_ : std::max(pnl.position, int64_t{0});
        }
        else if (bear_cross)
        {
            target = obi_confirms_short ? -max_qty_ : std::min(pnl.position, int64_t{0});
        }

        // ── Size and place one aggressive limit order ─────────────────────
        const int64_t delta = target - pnl.position;
        if (delta == 0)
            return;

        // ── Cooldown: block reversals only; exits bypass cooldown ──────────
        const bool is_reversal = (pnl.position > 0 && target < 0) || (pnl.position < 0 && target > 0);
        if (is_reversal && ticks_since_last_flip_ < min_ticks_between_flips_)
            return;

        Order o{};
        o.id = next_order_id();
        o.side = (delta > 0) ? Side::Buy : Side::Sell;
        o.qty = std::abs(delta);
        // Cross the spread for guaranteed fill at next LOB snapshot (T+1)
        o.price = (o.side == Side::Buy) ? lob.asks[0].price  // lift ask
                                        : lob.bids[0].price; // hit bid
        o.placement_ts = lob.timestamp;                      // T+1 fill semantics

        ob.submit(o);
        ++pnl.total_orders;
        if (is_reversal)
            ticks_since_last_flip_ = 0; // reset only on reversal
    }

    // ── Diagnostics ───────────────────────────────────────────────────────
    [[nodiscard]] double fast_ema() const noexcept { return fast_ema_; }
    [[nodiscard]] double slow_ema() const noexcept { return slow_ema_; }

  private:
    // ── Configuration ─────────────────────────────────────────────────────
    int64_t max_qty_;
    double alpha_fast_;
    double alpha_slow_;
    int32_t obi_depth_;
    double obi_threshold_;
    uint32_t warmup_ticks_;
    uint32_t min_ticks_between_flips_;
    double hysteresis_ticks_;

    // ── State ─────────────────────────────────────────────────────────────
    double fast_ema_ = 0.0;
    double slow_ema_ = 0.0;
    uint32_t ticks_seen_ = 0;
    uint32_t ticks_since_last_flip_ = std::numeric_limits<std::uint32_t>::max(); // no flip yet
};
