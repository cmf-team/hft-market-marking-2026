#include "exec/latency.hpp"

#include "exec/matcher.hpp"

#include "bt/events.hpp"
#include "bt/fill_sink.hpp"
#include "bt/latency_model.hpp"
#include "bt/order.hpp"
#include "bt/order_book.hpp"
#include "bt/queue_model.hpp"
#include "bt/types.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

// Test sink: records every event delivered to the strategy.
class RecordingSink final : public bt::IFillSink {
public:
    std::vector<bt::OrderId>      submitted;
    std::vector<bt::Fill>         fills;
    std::vector<bt::OrderReject>  rejects;
    std::vector<bt::OrderId>      cancel_acks;
    std::vector<bt::CancelReject> cancel_rejects;

    void on_submitted(bt::OrderId id) override { submitted.push_back(id); }
    void on_fill(const bt::Fill& f) override { fills.push_back(f); }
    void on_reject(const bt::OrderReject& r) override { rejects.push_back(r); }
    void on_cancel_ack(bt::OrderId id) override { cancel_acks.push_back(id); }
    void on_cancel_reject(const bt::CancelReject& r) override { cancel_rejects.push_back(r); }
};

bt::OrderBook book_with(bt::Price bid_top, bt::Price ask_top) {
    bt::BookSnapshot s{};
    s.ts = 0;
    s.bids[0] = { bid_top, 100 };
    s.asks[0] = { ask_top, 100 };
    bt::OrderBook b;
    b.apply(s);
    return b;
}

const bt::OrderBook kEmptyBook{};

struct Harness {
    bt::PessimisticQueueModel qm;
    bt::FixedLatencyModel     lm;
    bt::Matcher               matcher;
    RecordingSink             sink;
    bt::LatencySim            latency;

    Harness(bt::Timestamp submit_us, bt::Timestamp cancel_us, bt::Timestamp fill_us)
        : lm(submit_us, cancel_us, fill_us),
          matcher(qm),
          latency(lm, matcher, sink) {}
};

// --------------------------------------------------------------------------
// Submit delay
// --------------------------------------------------------------------------

TEST(LatencySim, SubmitIsVoidAndDoesNotReachMatcherImmediately) {
    Harness h(/*submit_us=*/100, /*cancel_us=*/100, /*fill_us=*/50);
    const auto book = book_with(100, 200);

    h.latency.submit(bt::Side::Buy, 150, 5, /*now=*/0);
    EXPECT_EQ(h.matcher.resting_count(), 0u);
    EXPECT_EQ(h.latency.pending_submits(), 1u);
    EXPECT_TRUE(h.sink.submitted.empty());

    // Flush at t=99: still 1us short of delivery — matcher unchanged.
    h.latency.flush_until(99, book);
    EXPECT_EQ(h.matcher.resting_count(), 0u);

    // Flush at t=100: order is now resting in the matcher; ack queued for
    // outbound delivery at 100 + 50 = 150.
    h.latency.flush_until(100, book);
    EXPECT_EQ(h.matcher.resting_count(), 1u);
    EXPECT_EQ(h.latency.pending_submits(), 0u);
    EXPECT_EQ(h.latency.pending_submitted(), 1u);
    EXPECT_TRUE(h.sink.submitted.empty());

    // At t=150 the on_submitted ack reaches the strategy.
    h.latency.flush_until(150, book);
    ASSERT_EQ(h.sink.submitted.size(), 1u);
    EXPECT_EQ(h.sink.submitted[0], 1u);  // matcher started at id=1
}

TEST(LatencySim, SubmittedAcksArriveInFifoOrder) {
    Harness h(/*submit_us=*/10, /*cancel_us=*/10, /*fill_us=*/10);
    const auto book = book_with(100, 200);

    h.latency.submit(bt::Side::Buy,  50,  1, 0);
    h.latency.submit(bt::Side::Buy,  51,  1, 0);
    h.latency.submit(bt::Side::Sell, 201, 1, 0);

    h.latency.flush_until(20, book);
    ASSERT_EQ(h.sink.submitted.size(), 3u);
    EXPECT_EQ(h.sink.submitted[0], 1u);
    EXPECT_EQ(h.sink.submitted[1], 2u);
    EXPECT_EQ(h.sink.submitted[2], 3u);
}

// --------------------------------------------------------------------------
// Cancel delay
// --------------------------------------------------------------------------

TEST(LatencySim, CancelOfRestingOrderIsDelayed) {
    Harness h(/*submit_us=*/10, /*cancel_us=*/100, /*fill_us=*/10);
    const auto book = book_with(100, 200);

    h.latency.submit(bt::Side::Buy, 150, 5, 0);
    h.latency.flush_until(20, book);  // submit + ack delivered (10 + 10)
    ASSERT_EQ(h.matcher.resting_count(), 1u);
    ASSERT_EQ(h.sink.submitted.size(), 1u);
    const bt::OrderId id = h.sink.submitted[0];

    // Strategy issues cancel at t=30 (after receiving the ack). cancel_delay=100
    // → matcher sees it at t=130.
    h.latency.cancel(id, 30);
    h.latency.flush_until(129, book);
    EXPECT_TRUE(h.matcher.has_order(id));  // not yet

    h.latency.flush_until(130, book);
    EXPECT_FALSE(h.matcher.has_order(id));
    EXPECT_EQ(h.matcher.resting_count(), 0u);
}

TEST(LatencySim, CancelAckIsDeliveredAfterFillDelay) {
    Harness h(/*submit_us=*/10, /*cancel_us=*/100, /*fill_us=*/50);
    const auto book = book_with(100, 200);

    h.latency.submit(bt::Side::Buy, 150, 5, 0);
    h.latency.flush_until(60, book);  // submit (10) + on_submitted ack (10+50=60)
    ASSERT_EQ(h.sink.submitted.size(), 1u);
    const bt::OrderId id = h.sink.submitted[0];

    h.latency.cancel(id, 70);
    // Cancel hits matcher at t=170, ack queued for delivery at 170 + 50 = 220.
    h.latency.flush_until(170, book);
    EXPECT_EQ(h.latency.pending_cancel_acks(), 1u);
    EXPECT_TRUE(h.sink.cancel_acks.empty());

    h.latency.flush_until(219, book);
    EXPECT_TRUE(h.sink.cancel_acks.empty());

    h.latency.flush_until(220, book);
    ASSERT_EQ(h.sink.cancel_acks.size(), 1u);
    EXPECT_EQ(h.sink.cancel_acks[0], id);
}

TEST(LatencySim, CancelOfUnknownOrderEmitsCancelReject) {
    Harness h(/*submit_us=*/10, /*cancel_us=*/100, /*fill_us=*/50);
    const auto book = book_with(100, 200);

    // Cancel an id the matcher has never seen.
    h.latency.cancel(/*id=*/424242, /*now=*/0);

    // Cancel reaches matcher at t=100; cancel-reject queued for delivery at 150.
    h.latency.flush_until(100, book);
    EXPECT_EQ(h.latency.pending_cancel_rejects(), 1u);
    EXPECT_TRUE(h.sink.cancel_rejects.empty());

    h.latency.flush_until(150, book);
    ASSERT_EQ(h.sink.cancel_rejects.size(), 1u);
    EXPECT_EQ(h.sink.cancel_rejects[0].id, 424242u);
    EXPECT_EQ(h.sink.cancel_rejects[0].reason, bt::CancelRejectReason::UnknownOrder);
}

TEST(LatencySim, CancelOfFilledOrderEmitsCancelReject) {
    Harness h(/*submit_us=*/10, /*cancel_us=*/100, /*fill_us=*/50);
    const auto book = book_with(100, 200);

    h.latency.submit(bt::Side::Buy, 150, 5, 0);
    h.latency.flush_until(60, book);  // submit + ack delivered
    ASSERT_EQ(h.sink.submitted.size(), 1u);
    const bt::OrderId id = h.sink.submitted[0];

    // Trade fully fills the order before the strategy's cancel arrives.
    h.matcher.on_trade(bt::Trade{ /*ts=*/65, bt::Side::Sell, /*price=*/150, /*amount=*/5 }, 65);
    ASSERT_FALSE(h.matcher.has_order(id));

    // Strategy (unaware) issues a cancel for the now-filled order.
    h.latency.cancel(id, /*now=*/70);

    // Drain everything; cancel-reject reaches the sink.
    h.latency.flush_until(10'000, book);
    ASSERT_EQ(h.sink.cancel_rejects.size(), 1u);
    EXPECT_EQ(h.sink.cancel_rejects[0].id, id);
    EXPECT_EQ(h.sink.cancel_rejects[0].reason, bt::CancelRejectReason::UnknownOrder);
    EXPECT_TRUE(h.sink.cancel_acks.empty());
}

// (No in-flight cancel test: at a real exchange the strategy can only refer
// to an id it has already received via on_submitted, so the latency layer
// doesn't model cancel-of-unacked-order.)

// --------------------------------------------------------------------------
// Fill delivery delay
// --------------------------------------------------------------------------

TEST(LatencySim, EnqueuedFillReachesSinkAfterFillDelay) {
    Harness h(/*submit_us=*/10, /*cancel_us=*/10, /*fill_us=*/200);
    const auto book = book_with(100, 200);

    const bt::Fill f{ /*id=*/42, /*ts=*/500, /*price=*/150, /*qty=*/3 };
    h.latency.enqueue_fill(f, /*now=*/500);
    EXPECT_EQ(h.latency.pending_fills(), 1u);
    EXPECT_TRUE(h.sink.fills.empty());

    h.latency.flush_until(699, book);
    EXPECT_TRUE(h.sink.fills.empty());

    h.latency.flush_until(700, book);
    ASSERT_EQ(h.sink.fills.size(), 1u);
    EXPECT_EQ(h.sink.fills[0].id, 42u);
    EXPECT_EQ(h.sink.fills[0].qty, 3);
}

TEST(LatencySim, MultipleFillsPreserveFifoOrder) {
    Harness h(10, 10, /*fill_us=*/100);
    const auto book = book_with(100, 200);

    h.latency.enqueue_fill(bt::Fill{ 1, 0, 150, 1 }, 0);
    h.latency.enqueue_fill(bt::Fill{ 2, 0, 150, 2 }, 0);
    h.latency.enqueue_fill(bt::Fill{ 3, 0, 150, 3 }, 0);

    h.latency.flush_until(100, book);
    ASSERT_EQ(h.sink.fills.size(), 3u);
    EXPECT_EQ(h.sink.fills[0].id, 1u);
    EXPECT_EQ(h.sink.fills[1].id, 2u);
    EXPECT_EQ(h.sink.fills[2].id, 3u);
}

// --------------------------------------------------------------------------
// Post-only reject path through the latency layer
// --------------------------------------------------------------------------

TEST(LatencySim, PostOnlyRejectIsDeliveredAfterFillDelay) {
    Harness h(/*submit_us=*/100, /*cancel_us=*/10, /*fill_us=*/50);
    const auto book = book_with(100, 200);

    // Buy at the best ask — would cross at delivery time.
    h.latency.submit(bt::Side::Buy, /*price=*/200, /*qty=*/5, /*now=*/0);

    // At t=100 the matcher processes the submit and rejects it; the reject
    // is queued for outbound delivery with fill_delay (=50). Sink should
    // not see it yet.
    h.latency.flush_until(100, book);
    EXPECT_EQ(h.matcher.resting_count(), 0u);
    EXPECT_EQ(h.latency.pending_rejects(), 1u);
    EXPECT_TRUE(h.sink.rejects.empty());
    // No on_submitted ack should be queued for a rejected submit.
    EXPECT_EQ(h.latency.pending_submitted(), 0u);

    // At t=149 still pending.
    h.latency.flush_until(149, book);
    EXPECT_TRUE(h.sink.rejects.empty());

    // At t=150 (= 100 + 50) the reject is delivered.
    h.latency.flush_until(150, book);
    ASSERT_EQ(h.sink.rejects.size(), 1u);
    EXPECT_EQ(h.sink.rejects[0].reason, bt::RejectReason::WouldCross);
    EXPECT_TRUE(h.sink.submitted.empty());  // never acked
}

// --------------------------------------------------------------------------
// Post-only check uses the book at delivery time, not at submit time
// --------------------------------------------------------------------------

TEST(LatencySim, PostOnlyChecksBookAtDeliveryNotSubmit) {
    Harness h(/*submit_us=*/100, /*cancel_us=*/10, /*fill_us=*/10);

    // (At submit time the strategy was looking at a book with best ask 200,
    // so a buy at 150 was passive. The latency layer doesn't use that book —
    // only the delivery-time book matters.)
    h.latency.submit(bt::Side::Buy, /*price=*/150, /*qty=*/5, /*now=*/0);

    // By the time the matcher sees the submit, the market has moved: best
    // ask is now 140, so a buy at 150 would cross. The reject should fire.
    const auto book_at_delivery = book_with(100, 140);
    h.latency.flush_until(100, book_at_delivery);

    EXPECT_EQ(h.matcher.resting_count(), 0u);
    EXPECT_EQ(h.latency.pending_rejects(), 1u);

    // Drain the reject through to the sink.
    h.latency.flush_until(1000, book_at_delivery);
    ASSERT_EQ(h.sink.rejects.size(), 1u);
    EXPECT_EQ(h.sink.rejects[0].reason, bt::RejectReason::WouldCross);
}

// --------------------------------------------------------------------------
// Cross-channel ordering: a submit and an enqueue_fill at the same `now`
// both end up at their respective sinks after their respective delays.
// --------------------------------------------------------------------------

TEST(LatencySim, MultipleChannelsAdvanceIndependently) {
    Harness h(/*submit_us=*/30, /*cancel_us=*/30, /*fill_us=*/70);
    const auto book = book_with(100, 200);

    h.latency.submit(bt::Side::Buy, 150, 5, /*now=*/0);
    h.latency.enqueue_fill(bt::Fill{ 99, 0, 150, 1 }, /*now=*/0);

    // At t=30 the submit reaches the matcher; the fill is still pending
    // (needs t=70). The on_submitted ack is also queued (fires at 30+70=100).
    h.latency.flush_until(30, book);
    EXPECT_EQ(h.matcher.resting_count(), 1u);
    EXPECT_TRUE(h.sink.fills.empty());
    EXPECT_TRUE(h.sink.submitted.empty());

    h.latency.flush_until(70, book);
    ASSERT_EQ(h.sink.fills.size(), 1u);
    EXPECT_EQ(h.sink.fills[0].id, 99u);

    h.latency.flush_until(100, book);
    ASSERT_EQ(h.sink.submitted.size(), 1u);
}

// --------------------------------------------------------------------------
// flush_until with no pending events is a no-op
// --------------------------------------------------------------------------

TEST(LatencySim, FlushWithEmptyQueuesIsNoOp) {
    Harness h(10, 10, 10);
    h.latency.flush_until(1'000'000, kEmptyBook);
    EXPECT_EQ(h.matcher.resting_count(), 0u);
    EXPECT_TRUE(h.sink.submitted.empty());
    EXPECT_TRUE(h.sink.fills.empty());
    EXPECT_TRUE(h.sink.rejects.empty());
}

}  // namespace
