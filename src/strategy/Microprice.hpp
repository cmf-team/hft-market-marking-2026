#pragma once

#include "engine/Types.hpp"

namespace cmf
{

inline double mid(const L2Snapshot& l2)
{
    if (l2.asks.empty() || l2.bids.empty())
        return 0.0;
    return 0.5 * (l2.asks.front().price + l2.bids.front().price);
}

// Stoikov (2018): probability-weighted expected future mid price.
// Large bid queue relative to ask queue pushes microprice toward the ask.
inline double microprice(const L2Snapshot& l2)
{
    if (l2.asks.empty() || l2.bids.empty())
        return 0.0;
    const double pa = l2.asks.front().price;
    const double pb = l2.bids.front().price;
    const double va = l2.asks.front().amount;
    const double vb = l2.bids.front().amount;
    const double total = va + vb;
    if (total <= 0.0)
        return 0.5 * (pa + pb);
    return (pa * vb + pb * va) / total;
}

} // namespace cmf
