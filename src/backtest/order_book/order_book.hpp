#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory_resource>
#include <queue>

#include "common/BasicTypes.hpp"

// ---------------------------------------------------------------------------
// Helper functions (order book management)
// ---------------------------------------------------------------------------
uint64_t next_order_id() noexcept;

// ---------------------------------------------------------------------------
// Unbounded OrderBook using standard C++ containers.
// - Bids sorted descending (highest price first = begin, std::greater)
// - Asks sorted ascending (lowest price first = begin, default)
// ---------------------------------------------------------------------------
class OrderBook
{
  public:
    using OrderQueue = std::pmr::deque<Order>;
    using BidLevelMap = std::pmr::map<int64_t, OrderQueue, std::greater<int64_t>>;
    using AskLevelMap = std::pmr::map<int64_t, OrderQueue>;

    explicit OrderBook(std::pmr::memory_resource* mr = std::pmr::get_default_resource());

    void submit(const Order& order);

    // -----------------------------------------------------------------------
    // match — invoke fn on the best order; auto-pops when fully consumed.
    // -----------------------------------------------------------------------
    template <typename Fn>
        requires std::invocable<Fn, Order&>
    [[nodiscard]] bool match(const Side side, Fn&& fn)
    {
        return side == Side::Buy
                   ? match_best(bids_, std::forward<Fn>(fn))
                   : match_best(asks_, std::forward<Fn>(fn));
    }

    [[nodiscard]] bool empty(const Side side) const noexcept;
    void drain(const Side side);

  private:
    template <typename Map, typename Fn>
    static bool match_best(Map& m, Fn&& fn)
    {
        if (m.empty())
            return false;
        auto it = m.begin(); // O(1), best level for both maps
        auto& q = it->second;
        auto& order = q.front();
        std::forward<Fn>(fn)(order);
        if (order.remaining() == 0) [[unlikely]]
        {
            q.pop_front();
            if (q.empty()) [[unlikely]]
                m.erase(it);
        }
        return true;
    }

    std::pmr::memory_resource* mr_;
    BidLevelMap bids_;
    AskLevelMap asks_;
};

// Thin wrapper around Order that adds a default constructor so that
// std::priority_queue<OrderV2, ...> can use pmr-allocated containers.
struct OrderV2 : Order
{
    OrderV2() = default;
    explicit OrderV2(const Order& o) : Order(o) {}
};

struct BuyCompare
{
    [[nodiscard]] bool operator()(const OrderV2& a, const OrderV2& b) const noexcept;
};

struct SellCompare
{
    [[nodiscard]] bool operator()(const OrderV2& a, const OrderV2& b) const noexcept;
};

// ---------------------------------------------------------------------------
// OrderBookV2 — heap-based order book using std::priority_queue.
// - Bids: max-heap (highest price first; older placement_ts; lower id)
// - Asks: min-heap (lowest price first; older placement_ts; lower id)
// Trades O(log n) insert/pop for O(1) best-price access via heap top.
// ---------------------------------------------------------------------------
class OrderBookV2
{
  public:
    using BuyContainer = std::pmr::vector<OrderV2>;
    using SellContainer = std::pmr::vector<OrderV2>;
    using BuyQueue = std::priority_queue<OrderV2, BuyContainer, BuyCompare>;
    using SellQueue = std::priority_queue<OrderV2, SellContainer, SellCompare>;

    explicit OrderBookV2(std::pmr::memory_resource* mr = std::pmr::get_default_resource());

    void submit(const Order& order);

    template <class Fn>
        requires std::invocable<Fn, Order&>
    [[nodiscard]] bool match(const Side side, Fn&& fn)
    {
        return side == Side::Buy
                   ? match_best(bids_, std::forward<Fn>(fn))
                   : match_best(asks_, std::forward<Fn>(fn));
    }

    [[nodiscard]] bool empty(const Side side) const noexcept;
    void drain(const Side side);

  private:
    template <class Queue, class Fn>
    static bool match_best(Queue& q, Fn&& fn)
    {
        if (q.empty())
            return false;

        auto current = q.top(); // O(1), heap top = best price
        q.pop();                // remove before fn; avoids dangling ref if fn re-submits

        std::forward<Fn>(fn)(static_cast<Order&>(current)); // strip V2 wrapper for fn

        if (current.remaining() > 0) [[likely]]
        { // partial fill: re-enqueue
            q.push(std::move(current));
        }
        return true;
    }

    std::pmr::memory_resource* mr_;
    BuyQueue bids_;
    SellQueue asks_;
};

// ---------------------------------------------------------------------------
// OrderBookLike concept — compile-time contract for order book implementations
// ---------------------------------------------------------------------------
template <typename T>
concept OrderBookLike =
    std::default_initializable<T> &&
    requires(T& ob, const Order& order, Side side) {
        { ob.submit(order) } -> std::same_as<void>;
        {
            ob.match(side, [](Order&) {})
        } -> std::same_as<bool>; // matches actual constraint
        { ob.empty(side) } -> std::same_as<bool>;
        { ob.drain(side) } -> std::same_as<void>;
    };
