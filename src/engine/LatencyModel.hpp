#pragma once

#include "engine/Types.hpp"
#include <random>

namespace cmf
{

struct LatencyModel
{
    virtual ~LatencyModel() = default;
    virtual NanoTime sample_delay(NanoTime now) = 0;
};

struct ConstLatency : LatencyModel
{
    NanoTime delay{0};
    explicit ConstLatency(NanoTime d) : delay(d) {}
    NanoTime sample_delay(NanoTime) override { return delay; }
};

struct LognormalLatency : LatencyModel
{
    std::mt19937_64 rng;
    std::lognormal_distribution<double> dist;
    NanoTime minClamp{0};

    LognormalLatency(std::uint64_t seed, double m, double s, NanoTime minClampNs = 0)
        : rng(seed), dist(m, s), minClamp(minClampNs)
    {
    }

    NanoTime sample_delay(NanoTime) override
    {
        const auto d = static_cast<NanoTime>(dist(rng));
        return d < minClamp ? minClamp : d;
    }
};

} // namespace cmf
