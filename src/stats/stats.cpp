#include "bt/stats.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <ostream>
#include <string>

namespace bt {

namespace {

// Format a Unix microsecond timestamp as ISO 8601 UTC with microsecond
// precision: "2024-08-01T01:20:02.982047Z". gmtime_r is used so the output
// is locale- and timezone-independent.
std::string format_us_iso(Timestamp us) {
    const long long secs_raw = static_cast<long long>(us / 1'000'000);
    long long       frac_raw = static_cast<long long>(us % 1'000'000);
    if (frac_raw < 0) { frac_raw += 1'000'000; }   // defensive — negative ts shouldn't appear
    // Narrow to a small unsigned so the compiler can prove the field width
    // bound and skip the format-truncation warning.
    const unsigned   frac = static_cast<unsigned>(frac_raw) % 1'000'000u;
    const std::time_t t   = static_cast<std::time_t>(secs_raw);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char date[32];
    std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &tm);
    char out[64];
    std::snprintf(out, sizeof(out), "%s.%06uZ", date, frac);
    return std::string(out);
}

}  // namespace

void Stats::on_fill(const Fill& fill) noexcept {
    ++fill_count_;
    total_volume_ += fill.qty;

    // Signed cash flow: a Buy fill is cash out (negative), a Sell fill is
    // cash in (positive). Summed across the run this gives gross PnL when
    // the position closes back to zero, and tracks open exposure otherwise.
    const std::int64_t notional = static_cast<std::int64_t>(fill.price) *
                                  static_cast<std::int64_t>(fill.qty);
    gross_pnl_ticks_ += (fill.side == Side::Buy) ? -notional : notional;

    position_ += (fill.side == Side::Buy) ? fill.qty : -fill.qty;

    fills_.push_back(FillSample{ fill.ts, fill.side, fill.price, fill.qty });
}

void Stats::on_mark(Timestamp now, std::int64_t equity_ticks) noexcept {
    // Track full time span unconditionally — even when downsampling, the
    // report header should reflect the true first/last event times.
    if (!have_mark_) {
        first_mark_ts_ = now;
        have_mark_     = true;
    }
    last_mark_ts_ = now;

    // Drawdown is updated on every mark so an intra-interval spike between
    // samples still counts.
    if (!have_peak_ || equity_ticks > peak_equity_) {
        peak_equity_ = equity_ticks;
        have_peak_   = true;
    }
    const std::int64_t dd = peak_equity_ - equity_ticks;
    if (dd > max_drawdown_) max_drawdown_ = dd;

    // Equity curve sampling. Always record the very first sample so the
    // curve starts at the run's beginning. After that, only push when the
    // configured interval has elapsed (or always, if interval == 0).
    const bool first = !have_sample_;
    const bool due   = first
                    || sample_interval_us_ <= 0
                    || (now - last_sample_ts_) >= sample_interval_us_;
    if (due) {
        equity_.push_back(EquityPoint{ now, equity_ticks });
        last_sample_ts_ = now;
        have_sample_    = true;
    }
}

double Stats::reject_rate() const noexcept {
    if (submitted_ == 0) return 0.0;
    return static_cast<double>(rejected_) / static_cast<double>(submitted_);
}

void Stats::write_summary(std::ostream& out, const InstrumentSpec& spec,
                          Price final_avg_entry_ticks) const {
    out << std::fixed << std::setprecision(8);
    out << "=== Backtest summary ===\n";
    if (have_mark_) {
        out << "start_ts_us:        " << first_mark_ts_ << '\n';
        out << "start_time_utc:     " << format_us_iso(first_mark_ts_) << '\n';
        out << "end_ts_us:          " << last_mark_ts_  << '\n';
        out << "end_time_utc:       " << format_us_iso(last_mark_ts_)  << '\n';
        const std::int64_t span_us = static_cast<std::int64_t>(last_mark_ts_ - first_mark_ts_);
        out << "duration_us:        " << span_us << '\n';
        out << "duration_seconds:   " << static_cast<double>(span_us) / 1'000'000.0 << '\n';
    }
    out << "tick_size:          " << spec.tick_size << '\n';
    out << "qty_scale:          " << spec.qty_scale << '\n';
    out << "submitted_orders:   " << submitted_   << '\n';
    out << "rejected_orders:    " << rejected_    << '\n';
    out << "reject_rate:        " << std::setprecision(4) << (reject_rate() * 100.0) << " %\n";
    out << std::setprecision(8);
    out << "fill_count:         " << fill_count_  << '\n';
    out << "total_volume:       " << from_qty(total_volume_, spec) << '\n';
    out << "final_position:     " << from_qty(position_, spec) << '\n';
    if (position_ != 0 && final_avg_entry_ticks != 0) {
        out << "final_avg_entry:    " << from_ticks(final_avg_entry_ticks, spec) << '\n';
    }

    // Final realized + unrealized PnL is the last equity sample (ticks ×
    // qty); convert to a human currency-per-unit value via tick_size.
    const std::int64_t final_equity_ticks = equity_.empty() ? 0 : equity_.back().equity_ticks;
    out << "final_equity_ticks: " << final_equity_ticks << '\n';
    out << "final_equity:       " << static_cast<double>(final_equity_ticks) * spec.tick_size << '\n';
    out << "max_drawdown_ticks: " << max_drawdown_ << '\n';
    out << "max_drawdown:       " << static_cast<double>(max_drawdown_) * spec.tick_size << '\n';
    out << "equity_samples:     " << equity_.size() << '\n';
}

void Stats::write_equity_csv(std::ostream& out, const InstrumentSpec& spec) const {
    out << "ts_us,equity\n";
    out << std::fixed << std::setprecision(10);
    for (const auto& p : equity_) {
        out << p.ts << ','
            << static_cast<double>(p.equity_ticks) * spec.tick_size << '\n';
    }
}

void Stats::write_fills_csv(std::ostream& out, const InstrumentSpec& spec) const {
    out << "ts_us,side,price,qty\n";
    out << std::fixed << std::setprecision(10);
    for (const auto& f : fills_) {
        out << f.ts << ','
            << (f.side == Side::Buy ? "buy" : "sell") << ','
            << from_ticks(f.price, spec) << ','
            << from_qty(f.qty, spec) << '\n';
    }
}

}  // namespace bt
