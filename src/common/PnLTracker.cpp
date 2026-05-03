#include "common/PnLTracker.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace cmf
{

void PnLTracker::onFill(const Fill& fill)
{
    Quantity signed_qty = (fill.side == Side::Buy) ? fill.quantity : -fill.quantity;

    if (first_timestamp_ == 0)
        first_timestamp_ = fill.timestamp;
    last_timestamp_ = fill.timestamp;

    ++total_fills_;
    if (fill.side == Side::Buy)
        ++buy_fills_;
    else
        ++sell_fills_;
    total_volume_ += fill.quantity;
    total_cost_ += fill.price * fill.quantity;
    turnover_ += fill.price * fill.quantity;

    Quantity old_pos = position_;
    Quantity new_pos = position_ + signed_qty;

    if (old_pos * signed_qty < 0)
    {
        Quantity closing_qty = std::min(std::abs(old_pos), fill.quantity);
        if (old_pos > 0)
            realized_pnl_ += closing_qty * (fill.price - avg_entry_price_);
        else
            realized_pnl_ += closing_qty * (avg_entry_price_ - fill.price);

        Quantity remaining = fill.quantity - closing_qty;
        if (remaining > 1e-9)
            avg_entry_price_ = fill.price;
        if (std::abs(new_pos) < 1e-9)
            avg_entry_price_ = 0;
    }
    else
    {
        if (std::abs(old_pos) < 1e-9)
        {
            avg_entry_price_ = fill.price;
        }
        else
        {
            avg_entry_price_ = (avg_entry_price_ * std::abs(old_pos) + fill.price * fill.quantity) /
                               (std::abs(old_pos) + fill.quantity);
        }
    }

    position_ = new_pos;
}

void PnLTracker::markToMarket(Price mid_price, MicroTime timestamp)
{
    last_mid_price_ = mid_price;
    last_timestamp_ = timestamp;

    Price equity = totalPnl();
    equity_curve_.push_back({timestamp, equity});
}

Price PnLTracker::unrealizedPnl() const
{
    if (std::abs(position_) < 1e-9 || last_mid_price_ == 0)
        return 0.0;
    if (position_ > 0)
        return position_ * (last_mid_price_ - avg_entry_price_);
    else
        return std::abs(position_) * (avg_entry_price_ - last_mid_price_);
}

Price PnLTracker::totalPnl() const
{
    return realized_pnl_ + unrealizedPnl();
}

PerformanceStats PnLTracker::stats() const
{
    PerformanceStats s{};
    s.realized_pnl = realized_pnl_;
    s.unrealized_pnl = unrealizedPnl();
    s.total_pnl = totalPnl();
    s.inventory = position_;
    s.turnover = turnover_;
    s.total_fills = total_fills_;
    s.buy_fills = buy_fills_;
    s.sell_fills = sell_fills_;
    s.total_volume = total_volume_;
    s.avg_fill_price = (total_volume_ > 0) ? total_cost_ / total_volume_ : 0;
    s.start_time = first_timestamp_;
    s.end_time = last_timestamp_;
    return s;
}

void PnLTracker::printReport() const
{
    auto s = stats();

    std::cout << "\n========== Report ==========\n"
              << std::fixed
              << "  PnL:        " << std::setprecision(6) << s.total_pnl << "\n"
              << "  Inventory:  " << std::setprecision(0) << s.inventory << "\n"
              << "  Turnover:   " << std::setprecision(4) << s.turnover << "\n"
              << "==============================\n";
}

void PnLTracker::saveEquityCurve(const std::string& path) const
{
    std::ofstream f(path);
    f << "timestamp,equity\n";
    f << std::fixed << std::setprecision(8);
    for (const auto& pt : equity_curve_)
        f << pt.timestamp << "," << pt.equity << "\n";
}

} // namespace cmf
