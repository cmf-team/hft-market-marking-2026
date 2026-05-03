// Утилиты для тестов backtest-движка.

#pragma once

#include "backtest/backtest_data_reader.hpp"

#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace hft_backtest::test
{

inline OrderBookSnapshot make_snap(uint64_t ts,
                                   std::vector<std::pair<Price, Quantity>> bids,
                                   std::vector<std::pair<Price, Quantity>> asks)
{
    OrderBookSnapshot s;
    s.timestamp_us = ts;
    s.bids = std::move(bids);
    s.asks = std::move(asks);
    return s;
}

// Ищем sample-данные относительно cwd теста. CTest стартует из build/, а
// если пользователь сам запускает `bin/test/hft-market-making-tests` -- из
// корня проекта. Поэтому пробуем оба варианта.
inline std::string find_sample_lob()
{
    const std::vector<std::string> candidates = {
        "sample_data/lob_sample.csv",
        "../sample_data/lob_sample.csv",
        "../../sample_data/lob_sample.csv",
    };
    for (const auto& p : candidates)
    {
        std::ifstream f(p);
        if (f.good()) return p;
    }
    return {};
}

} // namespace hft_backtest::test
