#include "bt/queue_model.hpp"

#include "bt/order_book.hpp"

#include <algorithm>

namespace bt {

Qty PessimisticQueueModel::initial_queue(Side side, Price price,
                                         const OrderBook& book) const noexcept {
    // book.volume_at returns 0 for prices that aren't one of the 25 levels,
    // which correctly handles "joined inside the spread" and "empty book".
    return book.volume_at(side, price);
}

Qty PessimisticQueueModel::on_trade(Order& order, Qty available) const noexcept {
    // Erode queue first. Anything not consumed by the queue is leftover
    // trade volume the matcher can fill against the order.
    const Qty consumed = std::min(available, order.queue_ahead);
    order.queue_ahead -= consumed;
    return available - consumed;
}

Qty PessimisticQueueModel::on_snapshot(Order& order,
                                       const OrderBook& prev,
                                       const OrderBook& curr) const noexcept {
    const Qty v_old = prev.volume_at(order.side, order.price);
    const Qty v_new = curr.volume_at(order.side, order.price);

    // Case A — price level disappeared. The market moved through us; the
    // order fills in full at its limit price. This is the safety-net path
    // for crossings the trade stream missed (data gaps, snapshot-only feeds,
    // batched updates).
    if (v_new == 0 && v_old > 0) {
        return order.qty - order.filled;
    }

    // Case B — volume decreased. Pessimistic attribution: cancellations
    // come from behind first, queue_ahead is unchanged. (Trades that ate
    // into the queue were already accounted for via on_trade between the
    // two snapshots.)
    if (v_new < v_old) {
        return 0;
    }

    // Case C — volume increased. New orders arrived behind us. queue_ahead
    // unchanged.
    // Case D — unchanged. Nothing to do.
    return 0;
}

}  // namespace bt
