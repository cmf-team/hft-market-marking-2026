#include "strategy/Intensity.hpp"
#include "io/CsvReader.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace cmf
{

namespace
{

struct OlsResult
{
    double a, b, r2;
};

OlsResult ols(const std::vector<double>& x, const std::vector<double>& y)
{
    const int n = static_cast<int>(x.size());
    if (n < 2)
        return {0.0, 0.0, 0.0};
    double sx = 0.0, sy = 0.0, sxy = 0.0, sxx = 0.0;
    for (int i = 0; i < n; ++i)
    {
        sx += x[i];
        sy += y[i];
        sxy += x[i] * y[i];
        sxx += x[i] * x[i];
    }
    const double denom = n * sxx - sx * sx;
    if (std::abs(denom) < 1e-15)
        return {sy / n, 0.0, 0.0};
    const double b = (n * sxy - sx * sy) / denom;
    const double a = (sy - b * sx) / n;
    const double yMean = sy / n;
    double ssTot = 0.0, ssRes = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double dy = y[i] - yMean;
        const double dr = y[i] - (a + b * x[i]);
        ssTot += dy * dy;
        ssRes += dr * dr;
    }
    const double r2 = ssTot > 0.0 ? 1.0 - ssRes / ssTot : 0.0;
    return {a, b, r2};
}

} // namespace

IntensityFit fit_intensity_from_csvs(
    const std::string& lobPath,
    const std::string& tradesPath,
    const FitConfig& cfg)
{
    CsvL2Reader l2Reader(lobPath);
    CsvTradesReader trdReader(tradesPath);

    L2Snapshot l2{};
    TradeEvent t{};
    bool hasL2 = l2Reader.next(l2);
    bool hasTrd = trdReader.next(t);

    std::vector<std::size_t> counts(static_cast<std::size_t>(cfg.buckets), 0);
    const double bucketWidth = cfg.maxDist / cfg.buckets;

    double lastMid = 0.0;
    NanoTime firstTs = 0, lastTs = 0;
    std::size_t tradeSeen = 0;

    while ((hasL2 || hasTrd) && tradeSeen < cfg.maxTrades)
    {
        if (hasL2 && (!hasTrd || l2.ts <= t.ts))
        {
            if (!l2.asks.empty() && !l2.bids.empty())
                lastMid = 0.5 * (l2.asks.front().price + l2.bids.front().price);
            hasL2 = l2Reader.next(l2);
            continue;
        }
        if (!hasTrd)
            break;
        if (lastMid <= 0.0)
        {
            hasTrd = trdReader.next(t);
            continue;
        }

        const double dist = std::abs(t.price - lastMid);
        if (dist < cfg.maxDist)
        {
            const int bin = std::min(cfg.buckets - 1,
                                     static_cast<int>(dist / bucketWidth));
            ++counts[static_cast<std::size_t>(bin)];
            if (firstTs == 0)
                firstTs = t.ts;
            lastTs = t.ts;
        }
        ++tradeSeen;
        hasTrd = trdReader.next(t);
    }

    const double spanSec = (lastTs > firstTs)
                               ? static_cast<double>(lastTs - firstTs) / 1e9
                               : 1.0;
    if (spanSec <= 0.0 || tradeSeen < 100)
        return {};

    std::vector<double> xs, ys;
    for (int i = 0; i < cfg.buckets; ++i)
    {
        const std::size_t cnt = counts[static_cast<std::size_t>(i)];
        if (cnt == 0)
            continue;
        const double delta = (i + 0.5) * bucketWidth;
        const double lambda = static_cast<double>(cnt) / spanSec;
        xs.push_back(delta);
        ys.push_back(std::log(lambda));
    }
    if (xs.size() < 3)
        return {};

    const auto res = ols(xs, ys);
    const double k = std::max(0.01, -res.b);
    const double A = std::exp(res.a);
    return {A, k, res.r2, true};
}

} // namespace cmf
