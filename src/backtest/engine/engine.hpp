#pragma once
#include "backtest/engine/args.hpp"
#include "backtest/order_book/order_book.hpp"
#include "backtest/parser/parser.hpp"
#include "common/BasicTypes.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

// ---------------------------------------------------------------------------
// Order book constants
// ---------------------------------------------------------------------------
inline constexpr size_t FILLS_RESERVE = 65536; // 2^16: power-of-2 heuristic for memory alignment & typical backtest runs
inline constexpr std::array SIDES{Side::Buy, Side::Sell};

// ---------------------------------------------------------------------------
// try_fill_on_lob — walk LOB levels until order is filled or no level crosses.
//   Buy  fills level[i] while asks[i].price <= order.price
//   Sell fills level[i] while bids[i].price >= order.price
//   Each level contributes min(remaining, level.amount) units.
//   Partial fills re-submit the order; fully filled orders are removed.
// ---------------------------------------------------------------------------
template <OrderBookLike OrderBookT = OrderBook>
void try_fill_on_lob(
    OrderBookT& ob,
    const LobSnapshot& lob,
    std::vector<Fill>& fills,
    PnlState& pnl,
    const Config& cfg)
{
    for (const Side our_side : SIDES)
    {
        const auto& levels = (our_side == Side::Buy) ? lob.asks : lob.bids;
        std::array<int64_t, LOB_DEPTH> lev_depleted = {};

        while (true)
        {
            bool advance = false;
            if (!ob.match(our_side, [&](std::reference_wrapper<Order> order_ref)
                          {
                              Order& best = order_ref;
                              if (lob.timestamp == best.placement_ts)
                                  return; // same-tick guard

                              bool touched = false;

                              for (int32_t lvl = 0; lvl < LOB_DEPTH && best.remaining() > 0; ++lvl)
                              {
                                  const int64_t price = levels[lvl].price;
                                  const bool crosses = (our_side == Side::Buy) ? (price <= best.price)
                                                                               : (price >= best.price);
                                  if (!crosses)
                                      break;
                                  const int64_t available = levels[lvl].amount - lev_depleted[lvl];
                                  if (available <= 0)
                                      continue; // level depleted, try next

                                  const int64_t fill_qty = std::min(best.remaining(), available);
                                  lev_depleted[lvl] += fill_qty;
                                  Fill f{lob.timestamp, best.id, price, fill_qty, our_side, 0.0};
                                  best.filled += fill_qty;
                                  pnl.apply_fill(f);
                                  const double notional = static_cast<double>(fill_qty) * static_cast<double>(price) / PRICE_SCALE_F;
                                  pnl.realized_pnl -= notional * (cfg.maker_fee_bps / BPS_PER_UNIT);
                                  f.running_realized = pnl.realized_pnl;
                                  fills.push_back(f);
                                  touched = true;
                              }

                              if (!touched)
                                  return; // no cross — stop
                              if (best.remaining() == 0)
                                  advance = true; // fully filled → loop for next
                              // remaining > 0: LOB liquidity exhausted — advance stays false → outer break
                          }))
                break; // ob.match returns false when book is empty
            if (!advance)
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// run_backtest — two-pointer merge of LOB + trade streams
// ---------------------------------------------------------------------------
struct BacktestResult
{
    uint64_t lob_rows = 0;
    PnlState pnl{};
    std::vector<Fill> fills;
    std::vector<double> pnl_series;       // subsampled total_pnl() snapshots
    std::vector<uint64_t> pnl_timestamps; // µs timestamps parallel to pnl_series
    double peak_pnl = 0.0;                // running peak of total_pnl()
    double max_drawdown = 0.0;            // per-tick max drawdown (absolute)
};

template <OrderBookLike OrderBookT = OrderBook, Strategy S>
BacktestResult run_backtest(LobReader& lob_rdr, S& strategy, const Config& cfg)
{
    std::pmr::unsynchronized_pool_resource pool;
    OrderBookT ob{&pool};

    BacktestResult res{};
    res.fills.reserve(FILLS_RESERVE);
    PnlState& pnl = res.pnl;

    LobSnapshot lob_snap{};
    bool have_lob = false; // true once we've received at least one LOB update

    while (true)
    {
        if (const uint64_t lob_ts = lob_rdr.peek_ts(); lob_ts == std::numeric_limits<std::uint64_t>::max())
            break;

        lob_rdr.peek(lob_snap);
        lob_rdr.advance();
        have_lob = true;
        ++res.lob_rows;

        pnl.update_unrealized(lob_snap.mid_ticks());
        try_fill_on_lob<OrderBookT>(ob, lob_snap, res.fills, pnl, cfg);
        strategy.template on_lob<OrderBookT>(lob_snap, ob, pnl);

        // Per-tick drawdown tracking — independent of pnl_sample_interval.
        const double total = pnl.total_pnl();
        if (total > res.peak_pnl)
            res.peak_pnl = total;
        const double dd = res.peak_pnl - total;
        if (dd > res.max_drawdown)
            res.max_drawdown = dd;

        if (cfg.pnl_sample_interval != 0 && res.lob_rows % cfg.pnl_sample_interval == 0)
        {
            res.pnl_series.push_back(total);
            res.pnl_timestamps.push_back(lob_snap.timestamp);
        }
    }

    // Final unrealized mark
    if (have_lob)
        pnl.update_unrealized(lob_snap.mid_ticks());

    return res;
}
