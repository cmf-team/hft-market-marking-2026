#include "engine/Metrics.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace cmf
{

double RealizedPnL::calculate() const
{
    double pnl = 0.0;
    for (const auto& e : trades)
        pnl += (e.side == Side::Sell ? 1.0 : -1.0) * e.qty * e.price - e.fee;
    return pnl;
}

double UnrealizedPnL::calculate() const
{
    return equity.empty() ? 0.0 : equity.back() - equity.front();
}

double Turnover::calculate() const
{
    double t = 0.0;
    for (const auto& e : trades)
        t += e.qty * e.price;
    return t;
}

double MaxAbsInventory::calculate() const
{
    double mx = 0.0;
    for (double v : inv)
        mx = std::max(mx, std::abs(v));
    return mx;
}

double TimeWeightedAvgInventory::calculate() const
{
    const std::size_t n = std::min(inv.size(), ts.size());
    if (n < 2)
        return inv.empty() ? 0.0 : std::abs(inv.front());
    double sumW = 0.0;
    double sumWV = 0.0;
    for (std::size_t i = 1; i < n; ++i)
    {
        const double dt = static_cast<double>(ts[i] - ts[i - 1]);
        sumW += dt;
        sumWV += std::abs(inv[i - 1]) * dt;
    }
    return sumW > 0.0 ? sumWV / sumW : 0.0;
}

double SharpeAnnualized::calculate() const
{
    if (rets.size() < 2)
        return 0.0;
    const double n = static_cast<double>(rets.size());
    const double mean = std::accumulate(rets.begin(), rets.end(), 0.0) / n;
    double var = 0.0;
    for (double r : rets)
        var += (r - mean) * (r - mean);
    var /= (n - 1.0);
    const double sd = std::sqrt(var);
    return sd > 0.0 ? mean / sd * std::sqrt(stepsPerYear) : 0.0;
}

double SortinoAnnualized::calculate() const
{
    if (rets.size() < 2)
        return 0.0;
    const double n = static_cast<double>(rets.size());
    const double mean = std::accumulate(rets.begin(), rets.end(), 0.0) / n;
    double downVar = 0.0;
    std::size_t cnt = 0;
    for (double r : rets)
    {
        if (r < 0.0)
        {
            downVar += r * r;
            ++cnt;
        }
    }
    if (cnt == 0)
        return 0.0;
    const double downSd = std::sqrt(downVar / static_cast<double>(cnt));
    return downSd > 0.0 ? mean / downSd * std::sqrt(stepsPerYear) : 0.0;
}

double MaxDrawdownPct::calculate() const
{
    double peak = -std::numeric_limits<double>::infinity();
    double maxDd = 0.0;
    for (double v : equity)
    {
        if (v > peak)
            peak = v;
        if (peak > 0.0)
        {
            const double dd = (peak - v) / peak * 100.0;
            if (dd > maxDd)
                maxDd = dd;
        }
    }
    return maxDd;
}

} // namespace cmf
