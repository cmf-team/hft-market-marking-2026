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

Matcher::SubmitResult Matcher::submit(OrderId id, Side side, Price price, Qty qty,
                                      const OrderBook& book, Timestamp now) {
    SubmitResult result;

    // Post-only check at delivery time. If the book is empty there's no
    // opposite side to cross, so the check is skipped.
    const bool buy_crosses  = (side == Side::Buy)  && !book.empty() && price >= book.best_ask();
    const bool sell_crosses = (side == Side::Sell) && !book.empty() && price <= book.best_bid();
    if (buy_crosses || sell_crosses) {
        result.accepted = false;
        result.reject = OrderReject{ id, now, RejectReason::WouldCross };
        return result;
    }

    Order o{};
    o.id           = id;
    o.side         = side;
    o.price        = price;
    o.qty          = qty;
    o.filled       = 0;
    o.submitted_ts = now;
    o.tif          = TimeInForce::PostOnly;
    // Initial queue position from the model. Joining inside the spread or
    // at a price not currently in the book yields 0.
    o.queue_ahead  = queue_model_->initial_queue(side, price, book);

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

Matcher::CancelResult Matcher::cancel(OrderId id, Timestamp /*now*/) {
    const auto idx_it = id_index_.find(id);
    if (idx_it == id_index_.end()) return CancelResult::UnknownOrder;
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
    return CancelResult::Cancelled;
}

std::vector<Fill> Matcher::on_trade(const Trade& trade, Timestamp now) {
    std::vector<Fill> fills;
    Qty remaining = trade.amount;

    // Walk one price level: thread the remaining trade volume through orders
    // in FIFO order. For each order, the queue model erodes its `queue_ahead`
    // first, then the matcher fills any leftover against the order. Fully
    // filled orders are popped; partially filled orders stay; orders whose
    // queue is still non-zero stay (next trade may continue eroding them).
    auto drain_level = [&](std::deque<Order>& level) {
        for (auto it = level.begin(); it != level.end() && remaining > 0;) {
            Order& o = *it;

            // Step 1: queue erosion. The model returns the leftover trade
            // volume after consuming queue_ahead.
            remaining = queue_model_->on_trade(o, remaining);

            // Step 2: fill, only if the queue cleared and there's still
            // trade volume to allocate.
            if (o.queue_ahead == 0 && remaining > 0) {
                const Qty want = o.qty - o.filled;
                const Qty take = std::min(want, remaining);
                o.filled  += take;
                remaining -= take;
                fills.push_back(Fill{ o.id, now, o.price, take, o.side });
                if (o.filled == o.qty) {
                    id_index_.erase(o.id);
                    it = level.erase(it);
                    continue;
                }
            }
            ++it;
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

std::vector<Fill> Matcher::on_snapshot(const OrderBook& prev,
                                       const OrderBook& curr,
                                       Timestamp now) {
    std::vector<Fill> fills;

    // Walk both sides. The queue model decides per-order whether the prev→curr
    // transition implies a fill (Case A — level disappeared) and updates
    // queue_ahead in place for cases B–D. Fully filled orders are removed.
    auto walk = [&](auto& book) {
        for (auto lvl_it = book.begin(); lvl_it != book.end();) {
            auto& level = lvl_it->second;
            for (auto it = level.begin(); it != level.end();) {
                Order& o = *it;
                const Qty fill_qty = queue_model_->on_snapshot(o, prev, curr);
                if (fill_qty > 0) {
                    // The model already capped against (qty - filled), but
                    // re-cap defensively.
                    const Qty take = std::min(fill_qty, o.qty - o.filled);
                    o.filled += take;
                    fills.push_back(Fill{ o.id, now, o.price, take, o.side });
                    if (o.filled == o.qty) {
                        id_index_.erase(o.id);
                        it = level.erase(it);
                        continue;
                    }
                }
                ++it;
            }
            if (level.empty()) {
                lvl_it = book.erase(lvl_it);
            } else {
                ++lvl_it;
            }
        }
    };
    walk(bids_);
    walk(asks_);

    return fills;
}

bool Matcher::has_order(OrderId id) const noexcept {
    return id_index_.contains(id);
}

}  // namespace bt
