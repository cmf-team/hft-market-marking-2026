#pragma once

#include "exec/latency.hpp"
#include "exec/matcher.hpp"

#include "bt/event_stream.hpp"
#include "bt/exchange.hpp"
#include "bt/fill_sink.hpp"
#include "bt/latency_model.hpp"
#include "bt/order.hpp"
#include "bt/order_book.hpp"
#include "bt/portfolio.hpp"
#include "bt/queue_model.hpp"
#include "bt/stats.hpp"
#include "bt/strategy.hpp"
#include "bt/types.hpp"

#include <cstdint>

namespace bt {

// The engine is the central wiring point: it owns the matcher, the latency
// simulator, the current and previous order books, and the portfolio. It
// drives the merged event stream and orchestrates the data flow:
//
//   stream → engine.run loop:
//     1. flush_until(now)              — release any due latency events
//     2a. on book event:
//         prev_book = book; book.apply(snap)
//         fills = matcher.on_snapshot(prev, curr, now)
//         portfolio.on_fill(...) for each; latency.enqueue_fill(..., now)
//         portfolio.mark_to_market(book.mid())
//         strategy.on_book(book, now)
//     2b. on trade event:
//         fills = matcher.on_trade(trade, now)
//         portfolio.on_fill(...) ; latency.enqueue_fill(..., now)
//         strategy.on_trade(trade)
//     3. drain at end with flush_until(INT64_MAX, book)
//
// The engine implements IExchange so the strategy can submit and cancel
// without seeing the latency layer directly. Submits/cancels coming from
// the strategy go straight into LatencySim — the matcher only sees them
// later, during a flush.
//
// The engine acts as the IFillSink for the latency layer: it intercepts
// every callback so it can update Stats, then forwards to the real
// strategy. PnL is booked at the matcher-side fill time for accuracy;
// the strategy is only *notified* after fill_delay.
class BacktestEngine final : public IExchange, public IFillSink {
public:
    BacktestEngine(MergedEventStream& stream,
                   const IQueueModel&  queue_model,
                   const ILatencyModel& latency_model,
                   IStrategy&          strategy) noexcept;

    // Run the backtest to completion. Returns the number of events processed.
    std::int64_t run();

    // Inspection (for tests / report writers).
    [[nodiscard]] const Portfolio& portfolio() const noexcept { return portfolio_; }
    [[nodiscard]] const OrderBook& book()      const noexcept { return book_; }
    [[nodiscard]] const Matcher&   matcher()   const noexcept { return matcher_; }
    [[nodiscard]] const Stats&     stats()     const noexcept { return stats_; }

    // ----- IExchange -----
    void post_only_limit(Timestamp now, Side side, Price price, Qty qty) override;
    void cancel(Timestamp now, OrderId id) override;

    // ----- IFillSink (intercepts then forwards to strategy) -----
    void on_submitted(OrderId id) override;
    void on_fill(const Fill& fill) override;
    void on_reject(const OrderReject& reject) override;
    void on_cancel_ack(OrderId id) override;
    void on_cancel_reject(const CancelReject& reject) override;

private:
    void deliver_fills_(const std::vector<Fill>& fills, Timestamp now);

    MergedEventStream* stream_;
    OrderBook          book_{};
    OrderBook          prev_book_{};
    Matcher            matcher_;
    LatencySim         latency_;
    Portfolio          portfolio_{};
    Stats              stats_{};
    IStrategy*         strategy_;
};

}  // namespace bt
