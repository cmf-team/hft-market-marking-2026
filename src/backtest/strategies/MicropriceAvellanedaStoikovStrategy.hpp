#pragma once
#include "backtest/order_book/order_book.hpp"
#include "common/BasicTypes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

// ---------------------------------------------------------------------------
// MicropriceAvellanedaStoikovStrategy
//
// Smart-but-simple extension:
// 1) Reference price = microprice instead of mid-price
// 2) A-S reservation price around that microprice
// 3) EMA volatility on microprice log-returns
// 4) Optional 2018-style size shaping via eta and inventory pressure
// 5) Cancel/repost both sides every LOB tick, matching your framework style
//
// Microprice at top-of-book:
//   micro = (best_ask * bid_sz + best_bid * ask_sz) / (bid_sz + ask_sz)
//
// A-S around fair price f:
//   r = f - q * gamma * sigma^2 * tau
//   delta = 0.5 * gamma * sigma^2 * tau + log(1 + gamma / k) / gamma
//
// Size shaping:
//   bid_qty = round(order_qty * (1 - eta * pressure))
//   ask_qty = round(order_qty * (1 + eta * pressure))
//   pressure = q / max_inventory in [-1, +1]
//
// Notes:
// - Keeps both sides live unless hard inventory bound blocks one side
// - Uses a passive depth clamp to avoid drifting too far from top of book
// - Uses exact cancel semantics via ob.drain() like your examples
// ---------------------------------------------------------------------------
class MicropriceAvellanedaStoikovStrategy
{
  public:
    explicit MicropriceAvellanedaStoikovStrategy(
        int64_t max_inventory,
        double gamma = 0.10,
        double k = 1.5,
        double horizon_ticks = 100.0,
        uint32_t vol_period = 200,
        uint32_t warmup_ticks = 200,
        int64_t order_qty = 2,
        double eta = 0.75,
        int64_t max_depth_ticks = 1) noexcept
        : max_inventory_(std::max<int64_t>(1, max_inventory)), gamma_(gamma), k_(k), horizon_ticks_(horizon_ticks), alpha_var_(2.0 / (static_cast<double>(vol_period) + 1.0)), warmup_ticks_(warmup_ticks), order_qty_(std::max<int64_t>(1, order_qty)), eta_(std::max(0.0, eta)), max_depth_ticks_(std::max<int64_t>(0, max_depth_ticks))
    {
    }

    [[nodiscard]] static uint64_t trade_rows() noexcept { return 0; }
    [[nodiscard]] static std::string_view name() noexcept { return "microprice_as"; }

    template <typename OrderBookT>
    void on_lob(const LobSnapshot& lob, OrderBookT& ob, PnlState& pnl) noexcept
    {
        using PriceT = decltype(lob.bids[0].price);

        const PriceT best_bid = lob.bids[0].price;
        const PriceT best_ask = lob.asks[0].price;
        const int64_t bid_sz = std::max<int64_t>(1, lob.bids[0].amount);
        const int64_t ask_sz = std::max<int64_t>(1, lob.asks[0].amount);

        const double mid =
            0.5 * (static_cast<double>(best_bid) + static_cast<double>(best_ask));

        const double micro =
            (static_cast<double>(best_ask) * static_cast<double>(bid_sz) +
             static_cast<double>(best_bid) * static_cast<double>(ask_sz)) /
            static_cast<double>(bid_sz + ask_sz);

        // Warmup on microprice log-returns
        if (ticks_seen_ < warmup_ticks_)
        {
            if (ticks_seen_ == 0)
            {
                ema_ref_ = micro;
                ema_var_ = 0.0;
            }
            else
            {
                const double log_ret = (ema_ref_ > 0.0) ? std::log(micro / ema_ref_) : 0.0;
                ema_var_ += alpha_var_ * (log_ret * log_ret - ema_var_);
                ema_ref_ += alpha_var_ * (micro - ema_ref_);
            }
            last_mid_ = mid;
            last_microprice_ = micro;
            ++ticks_seen_;
            return;
        }

        // Update variance on microprice
        const double log_ret = (ema_ref_ > 0.0) ? std::log(micro / ema_ref_) : 0.0;
        ema_var_ += alpha_var_ * (log_ret * log_ret - ema_var_);
        ema_ref_ += alpha_var_ * (micro - ema_ref_);

        // Always cancel old quotes first
        ob.drain(Side::Buy);
        ob.drain(Side::Sell);

        const double q = static_cast<double>(pnl.position);
        const double sigma2 = std::max(ema_var_, 1e-12);
        const double tau = horizon_ticks_;

        // Fair value = microprice, not raw mid
        fair_price_ = micro;

        reservation_price_ = fair_price_ - q * gamma_ * sigma2 * tau;

        half_spread_ =
            0.5 * gamma_ * sigma2 * tau +
            std::log(1.0 + gamma_ / k_) / gamma_;

        auto raw_bid = static_cast<PriceT>(std::llround(reservation_price_ - half_spread_));
        auto raw_ask = static_cast<PriceT>(std::llround(reservation_price_ + half_spread_));

        // Keep quotes passive and near top-of-book
        const PriceT max_depth = static_cast<PriceT>(max_depth_ticks_);
        PriceT bid_price = std::max<PriceT>(raw_bid, best_bid - max_depth);
        PriceT ask_price = std::min<PriceT>(raw_ask, best_ask + max_depth);

        if (bid_price >= best_ask)
            bid_price = static_cast<PriceT>(best_ask - 1);
        if (ask_price <= best_bid)
            ask_price = static_cast<PriceT>(best_bid + 1);
        if (ask_price <= bid_price)
            return;

        // 2018-style practical extension: shape order sizes with inventory
        const double pressure =
            std::clamp(q / static_cast<double>(max_inventory_), -1.0, 1.0);

        const int64_t bid_qty = std::clamp<int64_t>(
            static_cast<int64_t>(std::llround(
                static_cast<double>(order_qty_) * (1.0 - eta_ * pressure))),
            1,
            2 * order_qty_);

        const int64_t ask_qty = std::clamp<int64_t>(
            static_cast<int64_t>(std::llround(
                static_cast<double>(order_qty_) * (1.0 + eta_ * pressure))),
            1,
            2 * order_qty_);

        if (pnl.position < max_inventory_)
        {
            Order bid{};
            bid.id = next_order_id();
            bid.side = Side::Buy;
            bid.qty = std::min<int64_t>(bid_qty, max_inventory_ - pnl.position);
            bid.price = bid_price;
            bid.placement_ts = lob.timestamp;
            ob.submit(bid);
            ++pnl.total_orders;
        }

        if (pnl.position > -max_inventory_)
        {
            Order ask{};
            ask.id = next_order_id();
            ask.side = Side::Sell;
            ask.qty = std::min<int64_t>(ask_qty, max_inventory_ + pnl.position);
            ask.price = ask_price;
            ask.placement_ts = lob.timestamp;
            ob.submit(ask);
            ++pnl.total_orders;
        }

        last_mid_ = mid;
        last_microprice_ = micro;
        ++ticks_seen_;
    }

    [[nodiscard]] double fair_price() const noexcept { return fair_price_; }
    [[nodiscard]] double reservation_price() const noexcept { return reservation_price_; }
    [[nodiscard]] double half_spread() const noexcept { return half_spread_; }
    [[nodiscard]] double sigma2() const noexcept { return ema_var_; }
    [[nodiscard]] double last_microprice() const noexcept { return last_microprice_; }
    [[nodiscard]] double last_mid() const noexcept { return last_mid_; }

  private:
    int64_t max_inventory_;
    double gamma_;
    double k_;
    double horizon_ticks_;
    double alpha_var_;
    uint32_t warmup_ticks_;
    int64_t order_qty_;
    double eta_;
    int64_t max_depth_ticks_;

    uint32_t ticks_seen_ = 0;
    double ema_ref_ = 0.0;
    double ema_var_ = 0.0;

    double fair_price_ = 0.0;
    double reservation_price_ = 0.0;
    double half_spread_ = 0.0;
    double last_microprice_ = 0.0;
    double last_mid_ = 0.0;
};