#pragma once

#include "bt/types.hpp"

namespace bt {

// A strategy-submitted limit order. v1 only ships post-only limits, but the
// `tif` field is here so adding GTC/IOC later is mechanical.
struct Order {
    OrderId     id{};
    Side        side{};
    Price       price{};
    Qty         qty{};            // total requested
    Qty         filled{};         // running total of filled units
    Timestamp   submitted_ts{};   // when the matcher accepted it (post-latency)
    TimeInForce tif = TimeInForce::PostOnly;
    // Estimated volume sitting in front of this order at its price level.
    // Set on submit by the queue model and eroded by `on_trade` as public
    // trades chew through the queue. The order is only eligible for fills
    // once `queue_ahead` reaches 0.
    Qty         queue_ahead{};
};

// Notification of an executed quantity against an order. Emitted by the
// matcher and (in later steps) delivered to the strategy via the latency layer.
struct Fill {
    OrderId   id{};
    Timestamp ts{};
    Price     price{};   // resting order's limit price (no price improvement modeled)
    Qty       qty{};
};

// Notification that a submitted order was rejected by the matcher. v1 only
// produces these for post-only `WouldCross`. `id` is 0 because the matcher
// never assigned an id to a rejected order.
struct OrderReject {
    OrderId      id{};
    Timestamp    ts{};
    RejectReason reason{};
};

}  // namespace bt
