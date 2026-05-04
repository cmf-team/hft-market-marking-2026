#include "BasicTypes.hpp"

namespace cmf
{

std::size_t MarketSecurityIdHash::operator()(const MarketSecurityId& key) const noexcept
{
    std::size_t h1 = std::hash<SecurityId>{}(key.secId);
    std::size_t h2 = std::hash<MarketId>{}(key.mktId);
    return h1 ^ (h2 << 1);
}

} // namespace cmf

int64_t LobSnapshot::mid_ticks() const noexcept
{
    return (asks[0].price + bids[0].price) / 2;
}

double LobSnapshot::mid_price() const noexcept
{
    return static_cast<double>(asks[0].price + bids[0].price) / (2.0 * PRICE_SCALE_F);
}

int64_t Order::remaining() const noexcept
{
    return qty - filled;
}

void PnlState::apply_fill(const Fill& f) noexcept
{
    const int64_t signed_qty = (f.side == Side::Buy) ? +f.qty : -f.qty;
    const int64_t old_pos = position;
    position += signed_qty;
    ++total_fills;

    if (old_pos != 0 && ((old_pos > 0) != (signed_qty > 0)))
    {
        const int64_t closing = std::min(std::abs(old_pos), f.qty);
        const double pnl_per_unit = (f.side == Side::Buy)
                                        ? (avg_entry_ticks - static_cast<double>(f.price))
                                        : (static_cast<double>(f.price) - avg_entry_ticks);
        realized_pnl += static_cast<double>(closing) * pnl_per_unit / PRICE_SCALE_F;
    }

    if (position == 0)
    {
        avg_entry_ticks = 0.0;
    }
    else if ((old_pos == 0) || ((old_pos > 0) == (signed_qty > 0)))
    {
        const double total_cost = avg_entry_ticks * static_cast<double>(std::abs(old_pos)) + static_cast<double>(f.price) * static_cast<double>(f.qty);
        avg_entry_ticks = total_cost / static_cast<double>(std::abs(position));
    }
    else if ((old_pos > 0) != (position > 0))
    {
        avg_entry_ticks = static_cast<double>(f.price);
    }
}

void PnlState::update_unrealized(int64_t mid_t) noexcept
{
    if (position == 0)
    {
        unrealized_pnl = 0.0;
        return;
    }
    unrealized_pnl = static_cast<double>(position) * (static_cast<double>(mid_t) - avg_entry_ticks) / PRICE_SCALE_F;
}

double PnlState::total_pnl() const noexcept
{
    return realized_pnl + unrealized_pnl;
}
