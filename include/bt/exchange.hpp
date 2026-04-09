#pragma once

#include "bt/order.hpp"
#include "bt/types.hpp"

namespace bt {

// Strategy-facing facade for the order entry side of the exchange. The
// engine implements this on top of the LatencySim → Matcher pipeline; the
// strategy only ever sees this interface (never the matcher or the latency
// layer directly).
//
// Async-ack semantics: post_only_limit() returns void. The matcher assigns
// the OrderId when it actually processes the submit, and the strategy
// learns about it later via IStrategy::on_submitted (which is delivered
// through the same latency-delayed sink as fills/rejects).
//
// `now` is the current event timestamp the engine is processing. It is
// passed explicitly so the latency layer can stamp delivery times without
// the strategy having to know about a separate clock — there is no global
// "wall clock" inside a backtest. Strategies should always pass through
// the timestamp from the most recent on_book / on_trade callback.
struct IExchange {
    virtual ~IExchange() = default;

    // Submit a post-only limit. Returns immediately. The order id is
    // delivered to the strategy later via on_submitted, after submit_delay
    // (matcher-side) plus fill_delay (round-trip back to the strategy).
    // If the matcher rejects the submit at delivery time (would-cross),
    // on_reject is called instead — also after the same delay.
    virtual void post_only_limit(Timestamp now, Side side, Price price, Qty qty) = 0;

    // Cancel a previously-acked order. The strategy can only cancel ids it
    // has already received via on_submitted. If the cancel reaches the
    // matcher and the target id is unknown (filled or never existed), a
    // cancel-reject is delivered via on_cancel_reject; otherwise on_cancel_ack.
    virtual void cancel(Timestamp now, OrderId id) = 0;
};

}  // namespace bt
