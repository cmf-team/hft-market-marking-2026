#pragma once

#include <cmath>
#include <cstddef>
#include <limits>

namespace cmf
{

struct EwmaVariance
{
    double lambda;
    double s2{0.0};
    double prevLogPx{std::numeric_limits<double>::quiet_NaN()};
    std::size_t n{0};
    double cap;

    explicit EwmaVariance(double halfLifeTicks, double maxVar = 1e-3)
        : lambda(std::exp(-std::log(2.0) / halfLifeTicks)), cap(maxVar)
    {
    }

    void update(double px)
    {
        if (px <= 0.0)
            return;
        const double logPx = std::log(px);
        if (std::isnan(prevLogPx))
        {
            prevLogPx = logPx;
            return;
        }
        const double r = logPx - prevLogPx;
        prevLogPx = logPx;
        ++n;
        s2 = n == 1 ? std::min(cap, r * r)
                    : std::min(cap, lambda * s2 + (1.0 - lambda) * r * r);
    }

    double value() const { return s2; }
    bool initialized() const { return n >= 2; }
};

} // namespace cmf
