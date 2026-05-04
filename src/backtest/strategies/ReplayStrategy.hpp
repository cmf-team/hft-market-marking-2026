#pragma once
#include "backtest/order_book/order_book.hpp"
#include "backtest/parser/parser.hpp"
#include "common/BasicTypes.hpp"

// ---------------------------------------------------------------------------
// ReplayStrategy — replay trade events by placing opposite orders
// Consumes trades from trade_rdr inside on_lob for clean T+1 semantics:
// trades arriving before lob.timestamp are processed and orders are placed
// with placement_ts = lob.timestamp, so they can only fill at the next LOB (T+1)
// ---------------------------------------------------------------------------
class ReplayStrategy
{
    TradeReader& trade_rdr_;
    uint64_t trade_rows_ = 0;

  public:
    explicit ReplayStrategy(TradeReader& rdr) noexcept : trade_rdr_(rdr) {}

    [[nodiscard]] uint64_t trade_rows() const noexcept { return trade_rows_; }
    [[nodiscard]] static std::string_view name() noexcept { return "replay"; }

    template <OrderBookLike OrderBookT = OrderBook>
    void on_lob(const LobSnapshot& lob, OrderBookT& ob, PnlState& pnl) noexcept
    {
        TradeEvent trade{};
        while (trade_rdr_.peek_ts() < lob.timestamp)
        {
            trade_rdr_.peek(trade);
            trade_rdr_.advance();
            ++trade_rows_;
            Order o{};
            o.id = next_order_id();
            o.price = trade.price;
            o.qty = trade.amount;
            o.side = trade.side;
            o.placement_ts = lob.timestamp;
            ob.submit(o);
            ++pnl.total_orders;
        }
    }
};