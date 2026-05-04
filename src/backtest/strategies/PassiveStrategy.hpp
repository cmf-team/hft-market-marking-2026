#pragma once
#include "backtest/order_book/order_book.hpp"

// ---------------------------------------------------------------------------
// PassiveStrategy — market-make at best bid-1 / best ask+1 (symmetric quoting)
// ---------------------------------------------------------------------------
struct PassiveStrategy
{
    int64_t target_qty;
    explicit PassiveStrategy(const int64_t qty) : target_qty(qty) {}

    [[nodiscard]] static uint64_t trade_rows() noexcept { return 0; }
    [[nodiscard]] static std::string_view name() noexcept { return "passive"; }

    template <OrderBookLike OrderBookT = OrderBook>
    void on_lob(const LobSnapshot& lob, OrderBookT& ob, PnlState& pnl) const noexcept
    {
        if (pnl.position == 0)
        {
            ob.drain(Side::Buy);
            ob.drain(Side::Sell);
        }

        const int64_t target_bid = lob.bids[0].price - 1;
        const int64_t target_ask = lob.asks[0].price + 1;

        bool bid_stale = false;
        (void)ob.match(Side::Buy, [&](std::reference_wrapper<Order> b)
                       { bid_stale = (b.get().price != target_bid); });
        if (bid_stale)
            ob.drain(Side::Buy);

        bool ask_stale = false;
        (void)ob.match(Side::Sell, [&](std::reference_wrapper<Order> a)
                       { ask_stale = (a.get().price != target_ask); });
        if (ask_stale)
            ob.drain(Side::Sell);

        if (ob.empty(Side::Buy) && pnl.position < target_qty)
        {
            Order o{};
            o.id = next_order_id();
            o.price = target_bid;
            o.qty = target_qty - pnl.position;
            o.side = Side::Buy;
            o.placement_ts = lob.timestamp;
            ob.submit(o);
            ++pnl.total_orders;
        }

        if (ob.empty(Side::Sell) && pnl.position > -target_qty)
        {
            Order o{};
            o.id = next_order_id();
            o.price = target_ask;
            o.qty = target_qty + pnl.position;
            o.side = Side::Sell;
            o.placement_ts = lob.timestamp;
            ob.submit(o);
            ++pnl.total_orders;
        }
    }
};