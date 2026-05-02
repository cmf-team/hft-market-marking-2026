#pragma once

#include "analytics/pnl_tracker.hpp"
#include "common/BasicTypes.hpp"
#include "data/event_stream.hpp"
#include "exec/matching_engine.hpp"
#include "strategy/strategy_base.hpp"

#include <cstddef>
#include <string>

namespace cmf
{

class Backtester
{
  public:
    Backtester(const std::string& book_path, const std::string& trades_path);

    void run(StrategyBase* strategy = nullptr,
             MatchingEngine* me = nullptr,
             PnLTracker* pnl = nullptr);

    // Enable time-series CSV output sampled every `sample_every` book events.
    void enable_csv_output(const std::string& path, std::size_t sample_every = 100);

    const EventStream& stream() const noexcept { return stream_; }

    std::size_t book_events() const noexcept { return stream_.book_events(); }
    std::size_t trade_events() const noexcept { return stream_.trade_events(); }
    NanoTime last_timestamp() const noexcept { return last_ts_; }

  private:
    EventStream stream_;
    NanoTime last_ts_{0};
    std::string csv_path_;
    std::size_t csv_sample_every_{100};
};

} // namespace cmf
