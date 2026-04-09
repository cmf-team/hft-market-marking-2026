#include "bt/stats.hpp"

#include <algorithm>
#include <iomanip>
#include <ostream>

namespace bt {

void Stats::on_fill(const Fill& fill) noexcept {
    ++fill_count_;
    total_volume_ += fill.qty;

    // Signed cash flow: a Buy fill is cash out (negative), a Sell fill is
    // cash in (positive). Summed across the run this gives gross PnL when
    // the position closes back to zero, and tracks open exposure otherwise.
    const std::int64_t notional = static_cast<std::int64_t>(fill.price) *
                                  static_cast<std::int64_t>(fill.qty);
    gross_pnl_ticks_ += (fill.side == Side::Buy) ? -notional : notional;
}

void Stats::on_mark(Timestamp now, std::int64_t equity_ticks) noexcept {
    equity_.push_back(EquityPoint{ now, equity_ticks });
    if (!have_peak_ || equity_ticks > peak_equity_) {
        peak_equity_ = equity_ticks;
        have_peak_   = true;
    }
    const std::int64_t dd = peak_equity_ - equity_ticks;
    if (dd > max_drawdown_) max_drawdown_ = dd;
}

double Stats::reject_rate() const noexcept {
    if (submitted_ == 0) return 0.0;
    return static_cast<double>(rejected_) / static_cast<double>(submitted_);
}

void Stats::write_summary(std::ostream& out, const InstrumentSpec& spec) const {
    out << std::fixed << std::setprecision(8);
    out << "=== Backtest summary ===\n";
    out << "tick_size:          " << spec.tick_size << '\n';
    out << "qty_scale:          " << spec.qty_scale << '\n';
    out << "submitted_orders:   " << submitted_   << '\n';
    out << "rejected_orders:    " << rejected_    << '\n';
    out << "reject_rate:        " << std::setprecision(4) << (reject_rate() * 100.0) << " %\n";
    out << std::setprecision(8);
    out << "fill_count:         " << fill_count_  << '\n';
    out << "total_volume:       " << from_qty(total_volume_, spec) << '\n';

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
        out << p.ts << ',' << static_cast<double>(p.equity_ticks) * spec.tick_size << '\n';
    }
}

}  // namespace bt
