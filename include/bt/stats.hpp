#pragma once

#include "bt/order.hpp"
#include "bt/types.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <vector>

namespace bt {

// Run-time statistics collected by the engine. Counters and equity samples
// only — all formatting / file I/O is in the report writers below. Stats is
// completely independent of Portfolio: the engine feeds the running PnL into
// `on_mark` so Stats can keep its own equity curve and drawdown without
// peeking at the portfolio internals.
//
// PnL units are int64 ticks × qty throughout. The InstrumentSpec is only
// applied at the report-writer boundary.
class Stats {
public:
    Stats() = default;

    // Set the equity-curve sample interval in microseconds. The drawdown
    // peak/trough are still updated on every on_mark call (so a fast spike
    // between samples is captured), but the equity_ vector only grows once
    // per interval — at default 1s a multi-hour run produces a few thousand
    // points, not millions. Pass 0 to record every mark.
    void set_sample_interval(Timestamp interval_us) noexcept { sample_interval_us_ = interval_us; }

    // ----- Inputs (called by the engine) --------------------------------------

    // Strategy attempted to post a new order. Counted at the IExchange call
    // site so the metric is "orders sent" — independent of whether the
    // matcher later accepts or rejects them.
    void on_submit_attempt() noexcept { ++submitted_; }

    // The matcher rejected a submit (post-only would-cross at delivery time).
    void on_reject() noexcept { ++rejected_; }

    // A fill produced by the matcher. Engine calls this at matcher-side fill
    // time, before the latency-delayed delivery to the strategy.
    void on_fill(const Fill& fill) noexcept;

    // Sample the equity curve at `now`. The engine calls this after every
    // mark_to_market — i.e. after every book event. `equity_ticks` is
    // `portfolio.total_pnl_ticks()` (realized + unrealized).
    void on_mark(Timestamp now, std::int64_t equity_ticks) noexcept;

    // ----- Inspection ---------------------------------------------------------

    [[nodiscard]] std::size_t submitted_count() const noexcept { return submitted_; }
    [[nodiscard]] std::size_t rejected_count()  const noexcept { return rejected_; }
    [[nodiscard]] std::size_t fill_count()      const noexcept { return fill_count_; }
    [[nodiscard]] Qty         total_volume()    const noexcept { return total_volume_; }
    [[nodiscard]] std::int64_t gross_pnl_ticks() const noexcept { return gross_pnl_ticks_; }

    // Max drawdown is `peak_equity - trough` over the equity curve, in
    // int64 ticks × qty. Always >= 0.
    [[nodiscard]] std::int64_t max_drawdown_ticks() const noexcept { return max_drawdown_; }

    // Reject rate as a fraction in [0, 1]. Returns 0 when no submits.
    [[nodiscard]] double reject_rate() const noexcept;

    struct EquityPoint {
        Timestamp    ts;
        std::int64_t equity_ticks;
    };
    [[nodiscard]] const std::vector<EquityPoint>& equity_curve() const noexcept { return equity_; }

    // ----- Report writers -----------------------------------------------------

    // Human-readable summary. Includes the InstrumentSpec so the report is
    // self-describing — a reader can recover the human price/qty units from
    // the int-tick values without consulting the config.
    void write_summary(std::ostream& out, const InstrumentSpec& spec) const;

    // CSV equity curve: header `ts_us,equity` followed by one row per sample.
    // The equity column is converted to a human price units via `tick_size`
    // (so it has the same scale as a per-unit PnL). Volume is not included.
    void write_equity_csv(std::ostream& out, const InstrumentSpec& spec) const;

private:
    std::size_t  submitted_       = 0;
    std::size_t  rejected_        = 0;
    std::size_t  fill_count_      = 0;
    Qty          total_volume_    = 0;
    std::int64_t gross_pnl_ticks_ = 0;  // sum over fills of signed (price * qty);
                                        // not directly comparable to portfolio PnL,
                                        // but useful as a sanity figure in the report.

    // Equity-curve & drawdown state.
    std::vector<EquityPoint> equity_;
    std::int64_t             peak_equity_  = 0;
    std::int64_t             max_drawdown_ = 0;
    bool                     have_peak_    = false;

    // Sampling: drawdown is updated on every mark, but the equity_ vector
    // only stores points spaced by sample_interval_us_ (or every mark when
    // set to 0). first/last_mark_ts_ track the full backtest time span for
    // the report header.
    Timestamp                sample_interval_us_ = 1'000'000;  // default: 1 second
    Timestamp                last_sample_ts_     = 0;
    bool                     have_sample_        = false;
    Timestamp                first_mark_ts_      = 0;
    Timestamp                last_mark_ts_       = 0;
    bool                     have_mark_          = false;

public:
    [[nodiscard]] Timestamp first_mark_ts() const noexcept { return first_mark_ts_; }
    [[nodiscard]] Timestamp last_mark_ts()  const noexcept { return last_mark_ts_; }
    [[nodiscard]] bool      has_marks()     const noexcept { return have_mark_; }
};

}  // namespace bt
