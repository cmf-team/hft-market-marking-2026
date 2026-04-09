#pragma once

#include "bt/events.hpp"
#include "bt/order.hpp"
#include "bt/queue_model.hpp"
#include "bt/types.hpp"

#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bt {

class OrderBook;

// Internal matching engine. Deliberately NOT under include/bt/ — strategies
// must only see this through the latency-wrapped IExchange facade
// Keeping the header inside src/exec/ enforces that boundary at
// the include level: anything outside bt_core that tried to #include
// "exec/matcher.hpp" would not find it.

class Matcher {
public:

    struct SubmitResult {
        bool        accepted = false;
        OrderId     id{};         // valid iff accepted
        OrderReject reject{};     // valid iff !accepted
    };

    // Outcome of a cancel request. The matcher cannot distinguish "already
    // filled" from "never existed" — both look the same in the id_index_,
    // and that's exactly how a real exchange's CancelRejected payload works
    // (it just says ORDER_NOT_FOUND).
    enum class CancelResult { Cancelled, UnknownOrder };

    // The queue model is injected by reference — it is stateless and one
    // instance can be shared across the whole engine. The matcher does not
    // own its lifetime; the caller (the engine) keeps it alive.
    explicit Matcher(const IQueueModel& queue_model, OrderId starting_id = 1) noexcept
        : queue_model_(&queue_model), next_id_(starting_id) {}

    // Submit a post-only limit order. The matcher assigns the OrderId — the
    // strategy never sees it synchronously, only later via the latency-
    // delayed on_submitted callback. This models real exchange behavior
    // where the order id is part of the ack message that travels back over
    // the wire.
    //
    // The post-only check uses the book at *delivery* time, not at strategy
    // submit time, which is why an order that was passive when the strategy
    // sent it can still be rejected if the market moved during the latency
    // window.
    //
    // Acceptance: order is inserted at the back of its price level; the
    // assigned id is in result.id.
    // Rejection: nothing is inserted; result.reject carries the assigned id
    // and RejectReason::WouldCross. The id is consumed in either case so
    // the response is always self-identifying.
    SubmitResult submit(Side side, Price price, Qty qty,
                        const OrderBook& book, Timestamp now);

    // Cancel by id. Returns CancelResult so the caller (the latency layer)
    // can deliver a cancel-ack or cancel-reject back to the strategy. An
    // unknown id is not an error here — the order may have been filled
    // between strategy submit and cancel arrival.
    CancelResult cancel(OrderId id, Timestamp now);

    // Match an incoming public trade print against resting orders.
    //
    // Fill rule:
    //   - A Sell trade @ P fills resting Buys with price >= P (best bids first).
    //   - A Buy  trade @ P fills resting Sells with price <= P (best asks first).
    std::vector<Fill> on_trade(const Trade& trade, Timestamp now);

    // Snapshot-driven matching path.
    std::vector<Fill> on_snapshot(const OrderBook& prev, const OrderBook& curr,
                                  Timestamp now);

    // Test/inspection helpers.
    [[nodiscard]] bool        has_order(OrderId id) const noexcept;
    [[nodiscard]] std::size_t resting_count() const noexcept { return id_index_.size(); }

private:
    using BuyBook  = std::map<Price, std::deque<Order>, std::greater<>>;
    using SellBook = std::map<Price, std::deque<Order>>;

    const IQueueModel* queue_model_;
    OrderId            next_id_;
    BuyBook  bids_;
    SellBook asks_;

    std::unordered_map<OrderId, std::pair<Side, Price>> id_index_;
};

}  // namespace bt
