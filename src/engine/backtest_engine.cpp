#include "engine/backtest_engine.hpp"

#include <limits>
#include <variant>

namespace bt {

BacktestEngine::BacktestEngine(MergedEventStream&  stream,
                               const IQueueModel&  queue_model,
                               const ILatencyModel& latency_model,
                               IStrategy&          strategy) noexcept
    : stream_(&stream),
      matcher_(queue_model),
      latency_(latency_model, matcher_, strategy),
      strategy_(&strategy) {
    strategy_->set_exchange(this);
}

void BacktestEngine::deliver_fills_(const std::vector<Fill>& fills, Timestamp now) {
    for (const auto& f : fills) {
        // PnL updates immediately at the matcher-side fill time.
        portfolio_.on_fill(f);
        // The strategy hears about it after fill_delay.
        latency_.enqueue_fill(f, now);
    }
}

std::int64_t BacktestEngine::run() {
    Event event;
    std::int64_t count = 0;

    while (stream_->next(event)) {
        const Timestamp now = event_ts(event);

        // 1. Release any latency events that have come due. The book passed
        //    here is the *current* book — post-only checks at delivery time
        //    use it to decide whether a quoted submit would now cross.
        latency_.flush_until(now, book_);

        // 2. Dispatch the market event.
        std::visit([&](auto&& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, BookSnapshot>) {
                prev_book_ = book_;
                book_.apply(e);
                auto fills = matcher_.on_snapshot(prev_book_, book_, now);
                deliver_fills_(fills, now);
                portfolio_.mark_to_market(book_.mid());
                strategy_->on_book(book_, now);
            } else {
                static_assert(std::is_same_v<T, Trade>);
                auto fills = matcher_.on_trade(e, now);
                deliver_fills_(fills, now);
                strategy_->on_trade(e);
            }
        }, event);

        ++count;
    }

    // 3. Drain anything still in flight at end-of-run so the strategy sees
    //    its final acks/fills/rejects before the test inspects state.
    latency_.flush_until(std::numeric_limits<Timestamp>::max(), book_);
    return count;
}

void BacktestEngine::post_only_limit(Timestamp now, Side side, Price price, Qty qty) {
    latency_.submit(side, price, qty, now);
}

void BacktestEngine::cancel(Timestamp now, OrderId id) {
    latency_.cancel(id, now);
}

}  // namespace bt
