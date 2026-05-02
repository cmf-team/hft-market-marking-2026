#pragma once

#include "common/BasicTypes.hpp"
#include "exec/fill.hpp"

#include <cstddef>

namespace cmf
{

class PnLTracker
{
  public:
    PnLTracker() = default;

    void apply_fill(const Fill& fill);
    void mark_to_market(NanoTime ts, Price mid);

    Quantity position() const noexcept { return position_; }
    double cash() const noexcept { return cash_; }
    double equity() const noexcept { return last_equity_; }
    Price last_mid() const noexcept { return last_mid_; }
    NanoTime last_ts() const noexcept { return last_ts_; }

    std::size_t fill_count() const noexcept { return fill_count_; }
    Quantity volume() const noexcept { return volume_; }

    double max_equity() const noexcept { return max_equity_; }
    double min_equity() const noexcept { return min_equity_; }
    double max_drawdown() const noexcept { return max_dd_; }

  private:
    Quantity position_{0.0};
    double cash_{0.0};
    Price last_mid_{0.0};
    double last_equity_{0.0};
    NanoTime last_ts_{0};

    std::size_t fill_count_{0};
    Quantity volume_{0.0};

    double max_equity_{0.0};
    double min_equity_{0.0};
    double peak_equity_{0.0};
    double max_dd_{0.0};
    bool seen_equity_{false};
};

} // namespace cmf
