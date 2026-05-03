// Tracks inventory (signed position), realized + unrealized PnL, and turnover.
// Marks-to-market on demand and accumulates an equity curve.

#pragma once

#include "common/Types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace cmf
{

struct PerformanceStats
{
    Price realized_pnl;
    Price unrealized_pnl;
    Price total_pnl;
    Quantity inventory;
    Price turnover;
    int total_fills;
    int buy_fills;
    int sell_fills;
    Quantity total_volume;
    Price avg_fill_price;
    MicroTime start_time;
    MicroTime end_time;
};

class PnLTracker
{
  public:
    void onFill(const Fill& fill);
    void markToMarket(Price mid_price, MicroTime timestamp);

    Price realizedPnl() const { return realized_pnl_; }
    Price unrealizedPnl() const;
    Price totalPnl() const;
    Quantity inventory() const { return position_; }
    Price turnover() const { return turnover_; }

    PerformanceStats stats() const;
    void printReport() const;
    void saveEquityCurve(const std::string& path) const;

  private:
    Quantity position_ = 0;
    Price avg_entry_price_ = 0;
    Price realized_pnl_ = 0;

    Price last_mid_price_ = 0;

    int total_fills_ = 0;
    int buy_fills_ = 0;
    int sell_fills_ = 0;
    Quantity total_volume_ = 0;
    Price total_cost_ = 0;
    Price turnover_ = 0;

    MicroTime first_timestamp_ = 0;
    MicroTime last_timestamp_ = 0;

    struct EquityPoint
    {
        MicroTime timestamp;
        Price equity;
    };
    std::vector<EquityPoint> equity_curve_;
};

} // namespace cmf
