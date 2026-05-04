#include "exec/latency.hpp"

#include "exec/matcher.hpp"

namespace bt {

void LatencySim::submit(Side side, Price price, Qty qty, Timestamp now) {
    submits_.push_back(PendingSubmit{
        side, price, qty,
        now + model_->submit_delay(now)
    });
}

void LatencySim::cancel(OrderId id, Timestamp now) {
    // Pure delay buffer. The strategy can only cancel an id it has
    // received via on_submitted, so an unacked submit can never be the
    // target. If the order has been filled by the time the cancel arrives
    // at the matcher, matcher.cancel reports UnknownOrder and we deliver
    // a cancel-reject back to the strategy.
    cancels_.push_back(PendingCancel{
        id,
        now + model_->cancel_delay(now)
    });
}

void LatencySim::enqueue_fill(const Fill& fill, Timestamp now) {
    fills_.push_back(PendingFill{
        fill,
        now + model_->fill_delay(now)
    });
}

void LatencySim::flush_until(Timestamp now, const OrderBook& book) {
    // 1. Submits → matcher. The matcher assigns the id; on accept queue an
    //    on_submitted ack, on reject queue the reject. Both travel back
    //    with fill_delay starting from the matcher-side time.
    while (!submits_.empty() && submits_.front().delivery_ts <= now) {
        const auto p = submits_.front();
        submits_.pop_front();
        const auto r = matcher_->submit(p.side, p.price, p.qty, book, p.delivery_ts);
        const Timestamp out_ts = p.delivery_ts + model_->fill_delay(p.delivery_ts);
        if (r.accepted) {
            submitted_.push_back(PendingSubmitted{ r.id, out_ts });
        } else {
            rejects_.push_back(PendingReject{ r.reject, out_ts });
        }
    }

    // 2. Cancels → matcher. Cancelled → ack, UnknownOrder → cancel-reject.
    while (!cancels_.empty() && cancels_.front().delivery_ts <= now) {
        const auto p = cancels_.front();
        cancels_.pop_front();
        const auto result = matcher_->cancel(p.id, p.delivery_ts);
        const Timestamp out_ts = p.delivery_ts + model_->fill_delay(p.delivery_ts);
        if (result == Matcher::CancelResult::Cancelled) {
            cancel_acks_.push_back(PendingCancelAck{ p.id, out_ts });
        } else {
            cancel_rejects_.push_back(PendingCancelReject{
                CancelReject{ p.id, p.delivery_ts, CancelRejectReason::UnknownOrder },
                out_ts
            });
        }
    }

    // 3. Submitted acks → strategy.
    while (!submitted_.empty() && submitted_.front().delivery_ts <= now) {
        const auto p = submitted_.front();
        submitted_.pop_front();
        sink_->on_submitted(p.id);
    }

    // 4. Fills → strategy.
    while (!fills_.empty() && fills_.front().delivery_ts <= now) {
        const auto p = fills_.front();
        fills_.pop_front();
        sink_->on_fill(p.fill);
    }

    // 5. Rejects → strategy.
    while (!rejects_.empty() && rejects_.front().delivery_ts <= now) {
        const auto p = rejects_.front();
        rejects_.pop_front();
        sink_->on_reject(p.reject);
    }

    // 6. Cancel acks → strategy.
    while (!cancel_acks_.empty() && cancel_acks_.front().delivery_ts <= now) {
        const auto p = cancel_acks_.front();
        cancel_acks_.pop_front();
        sink_->on_cancel_ack(p.id);
    }

    // 7. Cancel rejects → strategy.
    while (!cancel_rejects_.empty() && cancel_rejects_.front().delivery_ts <= now) {
        const auto p = cancel_rejects_.front();
        cancel_rejects_.pop_front();
        sink_->on_cancel_reject(p.reject);
    }
}

}  // namespace bt
