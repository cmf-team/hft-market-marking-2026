#pragma once
#include "backtest/order_book/order_book.hpp"
#include "common/BasicTypes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

// ---------------------------------------------------------------------------
// AvellanedaStoikovPaperStrategy
// Exact finite-horizon Avellaneda & Stoikov (2008) core implementation
//
// r     = s - q * gamma * sigma^2 * (T - t)
// delta = 0.5 * gamma * sigma^2 * (T - t) + log(1 + gamma / k) / gamma
// bid   = r - delta
// ask   = r + delta
//
// Notes for this framework:
// - cancels both sides on every LOB update before reposting
// - uses fixed symmetric size (paper-style), no asymmetric sizing
// - uses constant sigma supplied as a parameter
// - clips quotes only to avoid crossing the book in the simulator
// ---------------------------------------------------------------------------
class AvellanedaStoikovStrategy
{
  public:
    explicit AvellanedaStoikovStrategy(
        int64_t max_inventory,
        double gamma = 0.05,
        double sigma = 2.0,
        double k = 1.5,
        uint32_t horizon_ticks = 100,
        int64_t order_qty = 1,
        uint32_t warmup_ticks = 0) noexcept
        : max_inventory_(max_inventory), gamma_(gamma), sigma_(sigma), k_(k), horizon_ticks_(horizon_ticks), order_qty_(order_qty), warmup_ticks_(warmup_ticks) {}

    [[nodiscard]] static uint64_t trade_rows() noexcept { return 0; }
    [[nodiscard]] static std::string_view name() noexcept { return "avellaneda_stoikov"; }

    template <typename OrderBookT>
    void on_lob(const LobSnapshot& lob, OrderBookT& ob, PnlState& pnl) noexcept
    {
        using PriceT = decltype(lob.bids[0].price);

        const PriceT best_bid = lob.bids[0].price;
        const PriceT best_ask = lob.asks[0].price;
        const double mid = 0.5 * static_cast<double>(best_bid + best_ask);

        if (ticks_seen_ < warmup_ticks_)
        {
            ++ticks_seen_;
            return;
        }

        const uint32_t t = ticks_seen_ - warmup_ticks_;
        ++ticks_seen_;

        // Cancel stale quotes first, same style as your other strategies.
        ob.drain(Side::Buy);
        ob.drain(Side::Sell);

        // Finite-horizon model: stop quoting after terminal time.
        if (t >= horizon_ticks_)
        {
            return;
        }

        const double tau = static_cast<double>(horizon_ticks_ - t);
        const double q = static_cast<double>(pnl.position);
        const double sigma2 = sigma_ * sigma_;

        reservation_price_ = mid - q * gamma_ * sigma2 * tau;
        half_spread_ = 0.5 * gamma_ * sigma2 * tau + std::log(1.0 + gamma_ / k_) / gamma_;

        PriceT bid_price = static_cast<PriceT>(
            std::llround(reservation_price_ - half_spread_));
        PriceT ask_price = static_cast<PriceT>(
            std::llround(reservation_price_ + half_spread_));

        // Practical simulator guard: never let quotes cross the current book.
        if (bid_price >= best_ask)
            bid_price = static_cast<PriceT>(best_ask - 1);
        if (ask_price <= best_bid)
            ask_price = static_cast<PriceT>(best_bid + 1);

        if (ask_price <= bid_price)
            return;

        if (pnl.position < max_inventory_)
        {
            Order bid{};
            bid.id = next_order_id();
            bid.side = Side::Buy;
            bid.qty = std::min<int64_t>(order_qty_, max_inventory_ - pnl.position);
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
            ask.qty = std::min<int64_t>(order_qty_, max_inventory_ + pnl.position);
            ask.price = ask_price;
            ask.placement_ts = lob.timestamp;
            ob.submit(ask);
            ++pnl.total_orders;
        }
    }

    [[nodiscard]] double reservation_price() const noexcept { return reservation_price_; }
    [[nodiscard]] double half_spread() const noexcept { return half_spread_; }

  private:
    int64_t max_inventory_;
    double gamma_;
    double sigma_;
    double k_;
    uint32_t horizon_ticks_;
    int64_t order_qty_;
    uint32_t warmup_ticks_;

    uint32_t ticks_seen_ = 0;
    double reservation_price_ = 0.0;
    double half_spread_ = 0.0;
};