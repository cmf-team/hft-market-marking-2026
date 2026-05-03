#pragma once

#include "engine/Types.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace cmf
{

struct Metric
{
    virtual ~Metric() = default;
    virtual std::string name() const = 0;
    virtual double calculate() const = 0;
};

struct RealizedPnL : Metric
{
    const std::vector<TradeRecord>& trades;
    explicit RealizedPnL(const std::vector<TradeRecord>& t) : trades(t) {}
    std::string name() const override { return "realized_pnl"; }
    double calculate() const override;
};

struct UnrealizedPnL : Metric
{
    const std::vector<double>& equity;
    explicit UnrealizedPnL(const std::vector<double>& e) : equity(e) {}
    std::string name() const override { return "unrealized_pnl"; }
    double calculate() const override;
};

struct Turnover : Metric
{
    const std::vector<TradeRecord>& trades;
    explicit Turnover(const std::vector<TradeRecord>& t) : trades(t) {}
    std::string name() const override { return "turnover"; }
    double calculate() const override;
};

struct MaxAbsInventory : Metric
{
    const std::vector<double>& inv;
    explicit MaxAbsInventory(const std::vector<double>& i) : inv(i) {}
    std::string name() const override { return "max_abs_inventory"; }
    double calculate() const override;
};

struct TimeWeightedAvgInventory : Metric
{
    const std::vector<double>& inv;
    const std::vector<NanoTime>& ts;
    TimeWeightedAvgInventory(const std::vector<double>& i, const std::vector<NanoTime>& t)
        : inv(i), ts(t)
    {
    }
    std::string name() const override { return "twap_inventory"; }
    double calculate() const override;
};

struct FillRatio : Metric
{
    std::uint64_t sent;
    std::uint64_t filled;
    FillRatio(std::uint64_t s, std::uint64_t f) : sent(s), filled(f) {}
    std::string name() const override { return "fill_ratio"; }
    double calculate() const override
    {
        return sent > 0 ? static_cast<double>(filled) / static_cast<double>(sent) : 0.0;
    }
};

struct SharpeAnnualized : Metric
{
    const std::vector<double>& rets;
    double stepsPerYear;
    SharpeAnnualized(const std::vector<double>& r, double spY) : rets(r), stepsPerYear(spY) {}
    std::string name() const override { return "sharpe"; }
    double calculate() const override;
};

struct SortinoAnnualized : Metric
{
    const std::vector<double>& rets;
    double stepsPerYear;
    SortinoAnnualized(const std::vector<double>& r, double spY) : rets(r), stepsPerYear(spY) {}
    std::string name() const override { return "sortino"; }
    double calculate() const override;
};

struct MaxDrawdownPct : Metric
{
    const std::vector<double>& equity;
    explicit MaxDrawdownPct(const std::vector<double>& e) : equity(e) {}
    std::string name() const override { return "max_drawdown_pct"; }
    double calculate() const override;
};

} // namespace cmf
