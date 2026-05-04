// defines basic types used throught the code

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace cmf
{

// Nanoseconds since Epoch
using NanoTime = std::int64_t;

using ClOrdId = std::uint64_t; // identifies client order id from trading system
                               // to broker/exch
using OrderId =
    std::uint64_t;                // identifies an order within a strategy on the market
using StrategyId = std::uint16_t; // identifies a strategy
using MarketId = std::uint16_t;   // identifies a market/exchange
using SecurityId =
    std::uint16_t; // identifies fungible security traded on 1 or more exchanges

using Quantity = double;
using Price = double;

enum class Side : signed short
{
    None = 0,
    Buy = 1,
    Sell = -1
};

enum class OrderType
{
    None = 0,
    Limit,
    Market
};

enum class TimeInForce
{
    None = 0,
    GoodTillCancel,
    FillAndKill,
    FillOrKill
};

enum class SecurityType
{
    None = 0,
    FX,
    Stock,
    Bond,
    Future,
    Option
};

// id for an object identifying a specific security traded on a specific market
struct MarketSecurityId
{
    MarketId mktId;
    SecurityId secId;

    bool operator==(const MarketSecurityId& other) const = default;
};

// hash function for MarketSecurityId
struct MarketSecurityIdHash
{
    std::size_t operator()(const MarketSecurityId& key) const noexcept
    {
        std::size_t h1 = std::hash<SecurityId>{}(key.secId);
        std::size_t h2 = std::hash<MarketId>{}(key.mktId);
        return h1 ^ (h2 << 1);
    }
};

// Market identifiers
class MktId
{
  public:
    // sentinel
    static constexpr MarketId None = 0;

    // TBD
};

// sentinel for SecurityId
struct SecId
{
    static constexpr SecurityId None = 0;
};

// sentinel for MarketSecurityId
struct MktSecId
{
    static constexpr MarketSecurityId None = {0, 0};
};

enum class EventType
{
    Quote,
    Trade
};

struct MarketEvent
{
    NanoTime ts_recv;
    EventType type;

    Price best_ask_price = 0;
    Quantity best_ask_size = 0;
    Price best_bid_price = 0;
    Quantity best_bid_size = 0;

    Price trade_price = 0;
    Quantity trade_size = 0;
    Side trade_side = Side::None;

    bool operator<(const MarketEvent& other) const
    {
        if (ts_recv == other.ts_recv)
        {
            return type == EventType::Quote && other.type == EventType::Trade;
        }
        return ts_recv < other.ts_recv;
    }
};

} // namespace cmf
