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
//   - submit() and cancel() are called synchronously by the strategy.
//     Events are queued with a per-channel delay and only forwarded to the
//     matcher when flush_until() advances time past their delivery timestamp.
//   - submit() returns the assigned OrderId synchronously so the strategy
//     can hold a reference to an order that hasn't yet reached the matcher.
//     Ids are owned by the latency layer (not the matcher) precisely so this
//     synchronous handle is available before the order is acknowledged.
//   - cancel() takes an OrderId. The strategy can only cancel an id it has
//     received back from submit(). There is no in-flight cancel handling at
//     the latency layer: if the cancel reaches the matcher and the order has
//     since been filled or rejected, matcher.cancel silently no-ops.
//
// Matcher → strategy direction:
//   - enqueue_fill() is called by the engine immediately after the matcher
//     produces a fill. The fill is delayed by fill_delay before being
//     delivered to the strategy.
//   - Rejects produced by matcher.submit() during a flush are captured
//     internally and queued for outbound delivery the same way.
//
// flush_until(now, book) is called by the engine at the top of each
// market-event iteration. The book reference is needed because the matcher's
// post-only check uses the book *at delivery time*, not at strategy submit
// time — that's how a quote that was passive when sent can still be
// rejected if the market moved during the latency window.
//
// The header is deliberately kept inside src/exec/ — strategies must only
// see this layer through the IExchange facade that lives in include/bt/.
class LatencySim {
public:
    LatencySim(const ILatencyModel& model, Matcher& matcher, IFillSink& sink,
               OrderId starting_id = 1) noexcept
        : model_(&model), matcher_(&matcher), sink_(&sink), next_id_(starting_id) {}

    // ----- Strategy-facing API -------------------------------------------------

    // Queue a post-only limit submission. Returns the assigned id immediately;
    // the matcher does not see the order until flush_until advances past
    // now + submit_delay.
    OrderId submit(Side side, Price price, Qty qty, Timestamp now);

    // Queue a cancel for an already-submitted order. Pure delay buffer — no
    // in-flight detection: if the cancel arrives at the matcher and the
    // target id is unknown, matcher.cancel silently no-ops.
    void cancel(OrderId id, Timestamp now);

    // ----- Matcher-facing API (called by the engine after matching) -----------

    // Queue a fill produced by the matcher. The portfolio update happens
    // synchronously in the engine; this only governs when the *strategy*
    // hears about the fill.
    void enqueue_fill(const Fill& fill, Timestamp now);

    // ----- Engine driver ------------------------------------------------------

    // Release every event whose delivery_ts <= now:
    //   1. Pending submits  → matcher.submit (using the live book). On
    //      accept the strategy is unaffected (it already has the id from
    //      the synchronous submit return); on reject the reject is queued
    //      for outbound delivery with fill_delay.
    //   2. Pending cancels  → matcher.cancel. The matcher returns whether
    //      the order was found; on success a cancel-ack is queued, on
    //      UnknownOrder a cancel-reject is queued. Both use fill_delay
    //      starting from the matcher-side delivery time.
    //   3. Pending fills          → sink.on_fill.
    //   4. Pending rejects        → sink.on_reject.
    //   5. Pending cancel-acks    → sink.on_cancel_ack.
    //   6. Pending cancel-rejects → sink.on_cancel_reject.
    void flush_until(Timestamp now, const OrderBook& book);

    // ----- Inspection (for tests) ---------------------------------------------

    [[nodiscard]] std::size_t pending_submits()        const noexcept { return submits_.size();        }
    [[nodiscard]] std::size_t pending_cancels()        const noexcept { return cancels_.size();        }
    [[nodiscard]] std::size_t pending_fills()          const noexcept { return fills_.size();          }
    [[nodiscard]] std::size_t pending_rejects()        const noexcept { return rejects_.size();        }
    [[nodiscard]] std::size_t pending_cancel_acks()    const noexcept { return cancel_acks_.size();    }
    [[nodiscard]] std::size_t pending_cancel_rejects() const noexcept { return cancel_rejects_.size(); }

private:
    struct PendingSubmit {
        OrderId   id;
        Side      side;
        Price     price;
        Qty       qty;
        Timestamp delivery_ts;
    };
    struct PendingCancel {
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
    OrderId              next_id_;

    std::deque<PendingSubmit>       submits_;
    std::deque<PendingCancel>       cancels_;
    std::deque<PendingFill>         fills_;
    std::deque<PendingReject>       rejects_;
    std::deque<PendingCancelAck>    cancel_acks_;
    std::deque<PendingCancelReject> cancel_rejects_;
};

}  // namespace bt
