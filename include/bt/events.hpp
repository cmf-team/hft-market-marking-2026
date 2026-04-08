#pragma once

#include "bt/types.hpp"

#include <array>
#include <cstddef>

namespace bt {

inline constexpr std::size_t kMaxLevels = 25;

struct PriceLevel {
    Price price{};
    Qty   amount{};
};

struct BookSnapshot {
    Timestamp ts{};
    std::array<PriceLevel, kMaxLevels> bids{};
    std::array<PriceLevel, kMaxLevels> asks{};
};

struct Trade {
    Timestamp ts{};
    Side      side{};
    Price     price{};
    Qty       amount{};
};

}  // namespace bt
