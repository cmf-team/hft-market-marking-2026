#pragma once

#include "bt/order.hpp"

namespace bt {

// Minimal outbound interface for the latency layer: anything that can
// receive fills and rejects from the matcher (after the latency delay).
// IStrategy will eventually inherit from this — for now it lets the
// latency tests use a tiny test sink without dragging in the full strategy
// interface.
struct IFillSink {
    virtual ~IFillSink() = default;

    // Delivered (with latency) after the matcher accepts a submit. This is
    // how the strategy learns the OrderId assigned by the matcher — the
    // submit() call itself returns nothing because, at a real exchange,
    // the id arrives in the ack message that travels back over the wire.
    // Acks are delivered FIFO, so the n-th submitted order produces the
    // n-th on_submitted callback.
    
    virtual void on_submitted(OrderId id)                        = 0;
    virtual void on_fill(const Fill& fill)                       = 0;
    virtual void on_reject(const OrderReject& reject)            = 0;
    virtual void on_cancel_ack(OrderId id)                       = 0;
    virtual void on_cancel_reject(const CancelReject& reject)    = 0;
};

}  // namespace bt
