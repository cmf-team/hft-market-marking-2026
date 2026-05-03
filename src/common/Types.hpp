#pragma once

#include "common/BasicTypes.hpp"

#include <array>
#include <cstdint>

namespace cmf
{

using MicroTime = std::int64_t;

struct Trade
{
    MicroTime timestamp;
    Side side;
    Price price;
    Quantity amount;
};

constexpr int kLobDepth = 25;

struct LOBLevel
{
    Price price;
    Quantity amount;
};

struct LOBSnapshot
{
    MicroTime timestamp;
    std::array<LOBLevel, kLobDepth> asks;
    std::array<LOBLevel, kLobDepth> bids;
};

enum class EventType : std::uint8_t
{
    Trade,
    LOBUpdate
};

struct Order
{
    OrderId id = 0;
    Side side = Side::None;
    OrderType type = OrderType::None;
    Price price = 0.0;
    Quantity quantity = 0.0;
    Quantity original_quantity = 0.0;
    MicroTime placed_at = 0;
};

struct Fill
{
    OrderId order_id;
    Side side;
    Price price;
    Quantity quantity;
    MicroTime timestamp;
};

} // namespace cmf
