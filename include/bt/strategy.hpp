#pragma once

#include "bt/events.hpp"
#include "bt/exchange.hpp"
#include "bt/fill_sink.hpp"
#include "bt/order.hpp"
#include "bt/order_book.hpp"
#include "bt/types.hpp"

namespace bt {

// Strategy interface. Implementers receive market events (book/trade) and
// async exchange callbacks (submitted/fill/reject/cancel-ack/cancel-reject)
// and place orders by calling through the IExchange* the engine injects.
//
// Strategies inherit from IFillSink so the latency layer can dispatch ack
// callbacks straight to them — there is no extra glue between the sink and
// the strategy. The engine wires `set_exchange()` once during construction
// and never touches it again.
//
// All callbacks are invoked synchronously by the engine on the single
// backtest thread; no locking required.
class IStrategy : public IFillSink {
public:
    ~IStrategy() override = default;

    // Called once by the engine before the run starts. The pointer remains
    // valid for the entire engine lifetime; the strategy does not own it.
    void set_exchange(IExchange* ex) noexcept { exchange_ = ex; }

    // Market events.
    virtual void on_book(const OrderBook& book, Timestamp now) = 0;
    virtual void on_trade(const Trade& trade)                  = 0;

    // IFillSink callbacks (delivered with latency by LatencySim):
    //   on_submitted(id)        — id assigned, order now resting (or about to be).
    //   on_fill(fill)           — partial or full fill of one of our orders.
    //   on_reject(reject)       — post-only would-cross at delivery time.
    //   on_cancel_ack(id)       — cancel reached the matcher and removed the order.
    //   on_cancel_reject(rej)   — cancel arrived but order was unknown (already filled, etc).

protected:
    IExchange* exchange_ = nullptr;
};

}  // namespace bt
