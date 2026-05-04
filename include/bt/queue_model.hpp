#pragma once

#include "bt/events.hpp"
#include "bt/order.hpp"
#include "bt/types.hpp"

namespace bt {

class OrderBook;

// Pluggable queue-position model. The matcher consults this on three events:
//   - submit: how much volume sits in front of a freshly posted order
//   - trade : how much of an incoming trade chews through the queue first
//   - snap  : how the queue position should change when the book moves
struct IQueueModel {
    virtual ~IQueueModel() = default;

    // Estimate of the volume queued ahead of an order being inserted at
    // (side, price) given the current book. 0 if the price is not currently
    // a level (joining inside the spread, or no resting volume yet).
    [[nodiscard]] virtual Qty initial_queue(Side side, Price price,
                                            const OrderBook& book) const noexcept = 0;

    // Erode `order.queue_ahead` against `available` units of trade volume
    // (which is the *currently remaining* volume of an incoming public trade
    // after orders ahead of `order` in the FIFO have already been processed).
    //
    // Returns the leftover volume after queue erosion — the matcher uses
    // this to fill the order, then threads the result into the next order
    // at the same price level.
    //
    // Caller guarantees: the matcher has already verified the trade crosses
    // the order's price; queue_ahead and filled are this model's to mutate.
    [[nodiscard]] virtual Qty on_trade(Order& order, Qty available) const noexcept = 0;

    // Apply a snapshot transition (prev → curr) to one resting order. The
    // model updates `order.queue_ahead` in place. Returns the quantity that
    // should be filled as a result of the transition (Case A — the price
    // level disappeared from the book), or 0 if no fill is implied.
    //
    // The matcher takes the returned qty, caps it at the order's remaining
    // size, and emits the Fill at the order's limit price.
    [[nodiscard]] virtual Qty on_snapshot(Order& order,
                                          const OrderBook& prev,
                                          const OrderBook& curr) const noexcept = 0;
};

// "Pessimistic" because cancellations are attributed to orders
// *behind* us first, not in front of us. This is conservative for the trader:
// it never optimistically advances queue position on volume drops.
//
// Stateless — one shared instance can serve every order.
class PessimisticQueueModel final : public IQueueModel {
public:
    [[nodiscard]] Qty initial_queue(Side side, Price price,
                                    const OrderBook& book) const noexcept override;

    [[nodiscard]] Qty on_trade(Order& order, Qty available) const noexcept override;

    [[nodiscard]] Qty on_snapshot(Order& order,
                                  const OrderBook& prev,
                                  const OrderBook& curr) const noexcept override;
};

}  // namespace bt
