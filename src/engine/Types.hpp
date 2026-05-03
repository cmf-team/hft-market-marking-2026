#pragma once

#include "common/BasicTypes.hpp"
#include <vector>

namespace cmf
{

struct Level
{
    Price price{0.0};
    Quantity amount{0.0};
};

struct L2Snapshot
{
    NanoTime ts{0};
    std::vector<Level> asks;
    std::vector<Level> bids;
};

struct TradeEvent
{
    NanoTime ts{0};
    Side side{Side::None};
    Price price{0.0};
    Quantity amount{0.0};
};

struct Order
{
    ClOrdId id{0};
    OrderType type{OrderType::None};
    Side side{Side::None};
    Price price{0.0};
    Quantity amount{0.0};
    NanoTime ts{0};
};

struct ExecReport
{
    ClOrdId orderId{0};
    Quantity filled{0.0};
    Price avgPrice{0.0};
    NanoTime ts{0};
};

struct TradeRecord
{
    NanoTime ts{0};
    Quantity qty{0.0};
    Price price{0.0};
    Side side{Side::None};
    double fee{0.0};
};

struct Fees
{
    double maker{0.0};
    double taker{0.0};
};

struct Portfolio
{
    double cash{0.0};
    Quantity position{0.0};
};

} // namespace cmf
