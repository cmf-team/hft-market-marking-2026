#include "exec/latency.hpp"

#include "exec/matcher.hpp"

#include <algorithm>

namespace bt {

OrderId LatencySim::submit(Side side, Price price, Qty qty, Timestamp now) {
    const OrderId id = next_id_++;
    submits_.push_back(PendingSubmit{
        id, side, price, qty,
        now + model_->submit_delay(now)
    });
    return id;
}

void LatencySim::cancel(OrderId id, Timestamp now) {
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
    // 1. Submits → matcher. Rejects from the post-only check are captured
    //    here and queued for outbound delivery.
    while (!submits_.empty() && submits_.front().delivery_ts <= now) {
        const auto p = submits_.front();
        submits_.pop_front();
        const auto r = matcher_->submit(p.id, p.side, p.price, p.qty, book, p.delivery_ts);
        if (!r.accepted) {
            rejects_.push_back(PendingReject{
                r.reject,
                p.delivery_ts + model_->fill_delay(p.delivery_ts)
            });
        }
    }

    // 2. Cancels → matcher. The matcher reports whether the order was found;
    //    queue an ack or a cancel-reject for outbound delivery.
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

    // 3. Fills → strategy.
    while (!fills_.empty() && fills_.front().delivery_ts <= now) {
        const auto p = fills_.front();
        fills_.pop_front();
        sink_->on_fill(p.fill);
    }

    // 4. Rejects → strategy.
    while (!rejects_.empty() && rejects_.front().delivery_ts <= now) {
        const auto p = rejects_.front();
        rejects_.pop_front();
        sink_->on_reject(p.reject);
    }

    // 5. Cancel acks → strategy.
    while (!cancel_acks_.empty() && cancel_acks_.front().delivery_ts <= now) {
        const auto p = cancel_acks_.front();
        cancel_acks_.pop_front();
        sink_->on_cancel_ack(p.id);
    }

    // 6. Cancel rejects → strategy.
    while (!cancel_rejects_.empty() && cancel_rejects_.front().delivery_ts <= now) {
        const auto p = cancel_rejects_.front();
        cancel_rejects_.pop_front();
        sink_->on_cancel_reject(p.reject);
    }
}

}  // namespace bt
