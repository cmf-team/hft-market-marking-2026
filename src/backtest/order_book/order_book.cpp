#include "order_book.hpp"

uint64_t next_order_id() noexcept
{
    static uint64_t ctr = 0;
    return ++ctr;
}

OrderBook::OrderBook(std::pmr::memory_resource* mr)
    : mr_(mr), bids_{mr_}, asks_{mr_}
{
}

void OrderBook::submit(const Order& order)
{
    if (order.side == Side::Buy)
        bids_[order.price].push_back(order);
    else
        asks_[order.price].push_back(order);
}

bool OrderBook::empty(const Side side) const noexcept
{
    return side == Side::Buy ? bids_.empty() : asks_.empty();
}

void OrderBook::drain(const Side side)
{
    if (side == Side::Buy)
        bids_.clear();
    else
        asks_.clear();
}

bool BuyCompare::operator()(const OrderV2& a, const OrderV2& b) const noexcept
{
    if (a.price != b.price)
        return a.price < b.price; // higher price first
    if (a.placement_ts != b.placement_ts)
        return a.placement_ts > b.placement_ts; // older first
    return a.id > b.id;                         // deterministic
}

bool SellCompare::operator()(const OrderV2& a, const OrderV2& b) const noexcept
{
    if (a.price != b.price)
        return a.price > b.price; // lower price first
    if (a.placement_ts != b.placement_ts)
        return a.placement_ts > b.placement_ts; // older first
    return a.id > b.id;                         // deterministic
}

OrderBookV2::OrderBookV2(std::pmr::memory_resource* mr)
    : mr_(mr), bids_(BuyCompare{}, BuyContainer{mr_}), asks_(SellCompare{}, SellContainer{mr_})
{
}

void OrderBookV2::submit(const Order& order)
{
    if (order.side == Side::Buy)
        bids_.emplace(order);
    else
        asks_.emplace(order);
}

bool OrderBookV2::empty(const Side side) const noexcept
{
    return side == Side::Buy ? bids_.empty() : asks_.empty();
}

void OrderBookV2::drain(const Side side)
{
    // priority_queue has no clear(); reconstruct to release memory via mr_.
    if (side == Side::Buy)
    {
        bids_ = BuyQueue{BuyCompare{}, BuyContainer{mr_}};
    }
    else
    {
        asks_ = SellQueue{SellCompare{}, SellContainer{mr_}};
    }
}
