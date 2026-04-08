#include "bt/order_book.hpp"

#include <cassert>

#include "bt/events.hpp"

namespace bt {

void OrderBook::apply(const BookSnapshot& snap) noexcept {
    snap_ = snap;
    populated_ = true;
}

Price OrderBook::best_bid() const noexcept {
    return populated_ ? snap_.bids[0].price : Price{0};
}

Price OrderBook::best_ask() const noexcept {
    return populated_ ? snap_.asks[0].price : Price{0};
}

Price OrderBook::mid() const noexcept {
    if (!populated_) return Price{0};
    return (snap_.bids[0].price + snap_.asks[0].price) / 2;
}

const PriceLevel& OrderBook::level(Side side, std::size_t depth) const noexcept {
    assert(depth < kMaxLevels);
    return (side == Side::Buy) ? snap_.bids[depth] : snap_.asks[depth];
}

Qty OrderBook::volume_at(Side side, Price price) const noexcept {
    if (!populated_) return Qty{0};
    const auto& side_levels = (side == Side::Buy) ? snap_.bids : snap_.asks;
    for (std::size_t i = 0; i < kMaxLevels; ++i) {
        if (side_levels[i].price == price) {
            return side_levels[i].amount;
        }
    }
    return Qty{0};
}

}  // namespace bt
