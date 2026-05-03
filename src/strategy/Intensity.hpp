#pragma once

#include <cstddef>
#include <string>

namespace cmf
{

struct FitConfig
{
    int buckets{20};
    double maxDist{5e-4};
    std::size_t maxTrades{2'000'000};
};

struct IntensityFit
{
    double A{1.0};
    double k{1.5};
    double r2{0.0};
    bool valid{false};
};

IntensityFit fit_intensity_from_csvs(
    const std::string& lobPath,
    const std::string& tradesPath,
    const FitConfig& cfg = {});

} // namespace cmf
