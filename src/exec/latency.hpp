#pragma once

#include "bt/fill_sink.hpp"
#include "bt/latency_model.hpp"
#include "bt/order.hpp"
#include "bt/types.hpp"

#include <cstddef>
#include <deque>

namespace bt {

class Matcher;
class OrderBook;

// Latency simulator. Sits between the strategy-facing IExchange (which the
// engine wires up later) and the internal Matcher.
//
// Strategy → matcher direction:
//   - submit() and cancel() are called synchronously by the strategy and
//     return immediately. The events are queued with a per-channel delay
//     and only forwarded to the matcher when flush_until() advances time
//     past their delivery timestamp.
//   - submit() returns void. Order ids are assigned by the matcher when it
//     processes the submit, and travel back to the strategy as a delayed
//     on_submitted callback through the sink — exactly like a real
//     exchange where the order id is part of the ack message that crosses
//     the network.
//   - cancel() takes an OrderId. The strategy can only cancel an id it has
//     already received via on_submitted. There is no in-flight cancel: if
//     the cancel reaches the matcher and the target id is unknown (because
//     the order has since been filled or never existed), the matcher
//     reports UnknownOrder and a cancel-reject is delivered back to the
//     strategy.
//
// Matcher → strategy direction:
//   - Acks for accepted submits are queued automatically during flush and
//     delivered with fill_delay (round-trip from the matcher back to the
//     strategy).
//   - Rejects produced by matcher.submit() during a flush are queued the
//     same way.
//   - Cancel-acks and cancel-rejects from matcher.cancel() are also queued
//     during flush.
//   - enqueue_fill() is called by the engine immediately after the matcher
//     produces a fill. The fill is delayed by fill_delay before being
//     delivered to the strategy.
//
// flush_until(now, book) is called by the engine at the top of each
// market-event iteration. The book reference is needed because the
// matcher's post-only check uses the book *at delivery time*, not at
// strategy submit time — that's how a quote that was passive when sent
// can still be rejected if the market moved during the latency window.
//
// The header is deliberately kept inside src/exec/ — strategies must only
// see this layer through the IExchange facade that lives in include/bt/.
class LatencySim {
public:
    LatencySim(const ILatencyModel& model, Matcher& matcher, IFillSink& sink) noexcept
        : model_(&model), matcher_(&matcher), sink_(&sink) {}

    // ----- Strategy-facing API -------------------------------------------------

    // Queue a post-only limit submission. Returns void; the matcher assigns
    // the id and the strategy learns it later via sink.on_submitted (after
    // submit_delay + fill_delay round-trip).
    void submit(Side side, Price price, Qty qty, Timestamp now);

    // Queue a cancel for an already-acked order. If the cancel arrives at
    // the matcher and the order is unknown, a cancel-reject is delivered
    // back to the strategy via the sink.
    void cancel(OrderId id, Timestamp now);

    // ----- Matcher-facing API (called by the engine after matching) -----------

    // Queue a fill produced by the matcher. The portfolio update happens
    // synchronously in the engine; this only governs when the *strategy*
    // hears about the fill.
    void enqueue_fill(const Fill& fill, Timestamp now);

    // ----- Engine driver ------------------------------------------------------

    // Release every event whose delivery_ts <= now:
    //   1. Pending submits        → matcher.submit. On accept queue an
    //                                on_submitted ack; on reject queue the
    //                                reject.
    //   2. Pending cancels        → matcher.cancel. On Cancelled queue a
    //                                cancel-ack; on UnknownOrder queue a
    //                                cancel-reject.
    //   3. Pending submitted-acks → sink.on_submitted.
    //   4. Pending fills          → sink.on_fill.
    //   5. Pending rejects        → sink.on_reject.
    //   6. Pending cancel-acks    → sink.on_cancel_ack.
    //   7. Pending cancel-rejects → sink.on_cancel_reject.
    void flush_until(Timestamp now, const OrderBook& book);

    // ----- Inspection (for tests) ---------------------------------------------

    [[nodiscard]] std::size_t pending_submits()        const noexcept { return submits_.size();        }
    [[nodiscard]] std::size_t pending_cancels()        const noexcept { return cancels_.size();        }
    [[nodiscard]] std::size_t pending_submitted()      const noexcept { return submitted_.size();      }
    [[nodiscard]] std::size_t pending_fills()          const noexcept { return fills_.size();          }
    [[nodiscard]] std::size_t pending_rejects()        const noexcept { return rejects_.size();        }
    [[nodiscard]] std::size_t pending_cancel_acks()    const noexcept { return cancel_acks_.size();    }
    [[nodiscard]] std::size_t pending_cancel_rejects() const noexcept { return cancel_rejects_.size(); }

private:
    struct PendingSubmit {
        Side      side;
        Price     price;
        Qty       qty;
        Timestamp delivery_ts;
    };
    struct PendingCancel {
        OrderId   id;
        Timestamp delivery_ts;
    };
    struct PendingSubmitted {
        OrderId   id;
        Timestamp delivery_ts;
    };
    struct PendingFill {
        Fill      fill;
        Timestamp delivery_ts;
    };
    struct PendingReject {
        OrderReject reject;
        Timestamp   delivery_ts;
    };
    struct PendingCancelAck {
        OrderId   id;
        Timestamp delivery_ts;
    };
    struct PendingCancelReject {
        CancelReject reject;
        Timestamp    delivery_ts;
    };

    // Each channel uses a simple deque + push_back. With a constant delay
    // and monotonically increasing `now`, delivery times are themselves
    // monotonic so popping from the front gives correct ordering. A jittered
    // model in the future would need a heap-ordered structure here.
    const ILatencyModel* model_;
    Matcher*             matcher_;
    IFillSink*           sink_;

    std::deque<PendingSubmit>       submits_;
    std::deque<PendingCancel>       cancels_;
    std::deque<PendingSubmitted>    submitted_;
    std::deque<PendingFill>         fills_;
    std::deque<PendingReject>       rejects_;
    std::deque<PendingCancelAck>    cancel_acks_;
    std::deque<PendingCancelReject> cancel_rejects_;
};

}  // namespace bt
