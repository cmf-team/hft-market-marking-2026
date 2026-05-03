#pragma once

#include <cstdint>

namespace strategy {

struct Analytics {
    double pnl = 0.0;
    double sharpe = 0.0;
    double win_rate = 0.0;

    uint64_t trades = 0;
    uint64_t wins = 0;
};

}