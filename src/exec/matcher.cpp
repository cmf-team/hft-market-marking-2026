#include "exec/matcher.hpp"

#include "bt/order_book.hpp"

#include <algorithm>
#include <utility>

namespace bt {

namespace {

// Drop the order with `id` from `level`. Returns true if found.
bool erase_from_level(std::deque<Order>& level, OrderId id) noexcept {
    for (auto it = level.begin(); it != level.end(); ++it) {
        if (it->id == id) {
            level.erase(it);
            return true;
        }
    }
    return false;
}

}  // namespace

Matcher::SubmitResult Matcher::submit(Side side, Price price, Qty qty,
                                      const OrderBook& book, Timestamp now) {
    SubmitResult result;

    // Post-only check at delivery time. If the book is empty there's no
    // opposite side to cross, so the check is skipped.
    const bool buy_crosses  = (side == Side::Buy)  && !book.empty() && price >= book.best_ask();
    const bool sell_crosses = (side == Side::Sell) && !book.empty() && price <= book.best_bid();
    if (buy_crosses || sell_crosses) {
        result.accepted = false;
        // id == 0: the matcher never assigned an id to a rejected order.
        result.reject = OrderReject{ /*id=*/0, now, RejectReason::WouldCross };
        return result;
    }

    Order o{};
    o.id           = next_id_++;
    o.side         = side;
    o.price        = price;
    o.qty          = qty;
    o.filled       = 0;
    o.submitted_ts = now;
    o.tif          = TimeInForce::PostOnly;

    if (side == Side::Buy) {
        bids_[price].push_back(o);
    } else {
        asks_[price].push_back(o);
    }
    id_index_.emplace(o.id, std::make_pair(side, price));

    result.accepted = true;
    result.id       = o.id;
    return result;
}

void Matcher::cancel(OrderId id, Timestamp /*now*/) {
    const auto idx_it = id_index_.find(id);
    if (idx_it == id_index_.end()) return;
    const auto [side, price] = idx_it->second;

    if (side == Side::Buy) {
        const auto lvl_it = bids_.find(price);
        if (lvl_it != bids_.end()) {
            erase_from_level(lvl_it->second, id);
            if (lvl_it->second.empty()) bids_.erase(lvl_it);
        }
    } else {
        const auto lvl_it = asks_.find(price);
        if (lvl_it != asks_.end()) {
            erase_from_level(lvl_it->second, id);
            if (lvl_it->second.empty()) asks_.erase(lvl_it);
        }
    }
    id_index_.erase(idx_it);
}

std::vector<Fill> Matcher::on_trade(const Trade& trade, Timestamp now) {
    std::vector<Fill> fills;
    Qty remaining = trade.amount;

    // Walk one price level: drain orders FIFO until either the level is
    // empty or the trade volume is exhausted. Fully filled orders are
    // popped and removed from the id index.
    auto drain_level = [&](std::deque<Order>& level) {
        while (!level.empty() && remaining > 0) {
            Order& o = level.front();
            const Qty want = o.qty - o.filled;
            const Qty take = std::min(want, remaining);
            o.filled  += take;
            remaining -= take;
            fills.push_back(Fill{ o.id, now, o.price, take });
            if (o.filled == o.qty) {
                id_index_.erase(o.id);
                level.pop_front();
            }
        }
    };

    if (trade.side == Side::Sell) {
        // Sell trade lifts our bids. Walk best bid downward; map is
        // ordered descending so the first iterator is the best price.
        for (auto it = bids_.begin(); it != bids_.end() && remaining > 0;) {
            if (it->first < trade.price) break;  // remaining levels are below the trade — no longer crossing
            drain_level(it->second);
            if (it->second.empty()) {
                it = bids_.erase(it);
            } else {
                ++it;
            }
        }
    } else {
        // Buy trade lifts our asks. Walk best ask upward.
        for (auto it = asks_.begin(); it != asks_.end() && remaining > 0;) {
            if (it->first > trade.price) break;
            drain_level(it->second);
            if (it->second.empty()) {
                it = asks_.erase(it);
            } else {
                ++it;
            }
        }
    }

    return fills;
}

std::vector<Fill> Matcher::on_snapshot(const OrderBook& /*prev*/,
                                       const OrderBook& /*curr*/,
                                       Timestamp /*now*/) {

    return {};
}

bool Matcher::has_order(OrderId id) const noexcept {
    return id_index_.contains(id);
}

}  // namespace bt
