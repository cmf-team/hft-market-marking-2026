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
    std::size_t operator()(const MarketSecurityId& key) const noexcept;
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

} // namespace cmf

// ---------------------------------------------------------------------------
// Backtest types (global namespace)
// ---------------------------------------------------------------------------

// Price representation: int64_t ticks = price × PRICE_SCALE (1e7)
// e.g. 0.0110436 → 110436; int64_t handles prices up to ~922 trillion ticks
static constexpr int64_t PRICE_SCALE = 10'000'000;
static constexpr double PRICE_SCALE_F = 10'000'000.0;
static constexpr int32_t PRICE_SCALE_DIG = 7; // fractional digits kept

// Fee representation: 1 basis point = 0.01% = 1/10'000
static constexpr double BPS_PER_UNIT = 10'000.0;

// Time constants (microseconds)
static constexpr uint64_t US_PER_DAY = 86'400'000'000ULL;

// Order book depth
static constexpr int32_t LOB_DEPTH = 25;

struct Level
{
    int64_t price;  // ticks
    int64_t amount; // raw integer units
};

struct LobSnapshot
{
    uint64_t timestamp;    // microseconds
    Level asks[LOB_DEPTH]; // asks[0] = best ask (lowest price)
    Level bids[LOB_DEPTH]; // bids[0] = best bid (highest price)

    [[nodiscard]] int64_t mid_ticks() const noexcept;
    [[nodiscard]] double mid_price() const noexcept;
};

enum class Side : uint8_t
{
    Buy = 0,
    Sell = 1
};

struct TradeEvent
{
    uint64_t timestamp; // microseconds
    int64_t price;      // ticks
    int64_t amount;     // raw integer
    Side side;
};

struct Order
{
    uint64_t id = 0;
    int64_t price = 0; // limit price in ticks
    int64_t qty = 0;
    int64_t filled = 0;
    Side side = Side::Buy;
    uint64_t placement_ts = 0; // timestamp when order was placed (µs)

    [[nodiscard]] int64_t remaining() const noexcept;
};

struct Fill
{
    uint64_t ts;
    uint64_t order_id;
    int64_t price; // ticks
    int64_t qty;
    Side side;
    double running_realized; // realized PnL snapshot after this fill
};

struct PnlState
{
    int64_t position = 0;
    double realized_pnl = 0.0;
    double unrealized_pnl = 0.0;
    double avg_entry_ticks = 0.0; // VWAP of open position in ticks
    uint64_t total_fills = 0;
    uint64_t total_orders = 0;

    void apply_fill(const Fill& f) noexcept;
    void update_unrealized(int64_t mid_t) noexcept;
    [[nodiscard]] double total_pnl() const noexcept;
};

// C++20 Strategy concept
class OrderBook; // forward declaration

template <typename T>
concept Strategy = requires(T& s, const T& cs,
                            const LobSnapshot& lob,
                            OrderBook& ob,
                            PnlState& pnl) {
    { s.on_lob(lob, ob, pnl) } -> std::same_as<void>;
    { cs.trade_rows() } -> std::same_as<uint64_t>;
    { cs.name() } -> std::same_as<std::string_view>;
};
