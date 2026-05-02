#include "book/order_book.hpp"

#include <stdexcept>

namespace cmf {

void OrderBook::update(NanoTime                          ts,
                       const std::array<PriceLevel, kDepth>&  asks,
                       const std::array<PriceLevel, kDepth>&  bids) {
    ts_   = ts;
    asks_ = asks;
    bids_ = bids;
}

PriceLevel OrderBook::best_ask() const noexcept {
    return asks_[0];
}

PriceLevel OrderBook::best_bid() const noexcept {
    return bids_[0];
}

Price OrderBook::mid_price() const noexcept {
    return 0.5 * (asks_[0].price + bids_[0].price);
}

Price OrderBook::spread() const noexcept {
    return asks_[0].price - bids_[0].price;
}

PriceLevel OrderBook::ask_at(std::size_t level) const {
    if (level >= kDepth) {
        throw std::out_of_range("OrderBook::ask_at: level out of range");
    }
    return asks_[level];
}

PriceLevel OrderBook::bid_at(std::size_t level) const {
    if (level >= kDepth) {
        throw std::out_of_range("OrderBook::bid_at: level out of range");
    }
    return bids_[level];
}

bool OrderBook::empty() const noexcept {
    return asks_[0].amount == 0.0 && bids_[0].amount == 0.0;
}

}  // namespace cmf
