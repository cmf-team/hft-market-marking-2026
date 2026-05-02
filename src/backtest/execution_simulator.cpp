#include "backtest/execution_simulator.hpp"
#include <algorithm>

namespace hft_backtest {

ExecutionSimulator::ExecutionSimulator(const ExecutionConfig& cfg) : cfg_(cfg) {}

std::vector<RestingOrder>
ExecutionSimulator::apply_action(const StrategyAction& action, uint64_t now_us) {
    for (const auto& cancel : action.cancels) {
        if (cancel.cancel_all) {
            orders_cancelled_ += book_.size();
            book_.clear();
        } else {
            auto it = std::find_if(book_.begin(), book_.end(),
                                   [&](const RestingOrder& o) { return o.id == cancel.order_id; });
            if (it != book_.end()) {
                ++orders_cancelled_;
                book_.erase(it);
            }
        }
    }

    std::vector<RestingOrder> placed;
    placed.reserve(action.quotes.size());
    for (const auto& q : action.quotes) {
        if (q.quantity == 0 || q.price == 0) continue;
        RestingOrder order{next_order_id_++, q.side, q.price, q.quantity, now_us};
        book_.push_back(order);
        placed.push_back(order);
        ++orders_placed_;
    }
    return placed;
}

std::vector<FillReport>
ExecutionSimulator::match_against_book(const OrderBookSnapshot& snap, uint64_t now_us) {
    std::vector<FillReport> fills;
    if (snap.bids.empty() || snap.asks.empty()) return fills;

    const double slippage_mult = cfg_.slippage_bps / 10000.0;

    for (auto it = book_.begin(); it != book_.end(); ) {
        if (cfg_.queue_priority && it->placed_ts_us == now_us) {
            ++it;
            continue;
        }

        // Walk-the-book: проходим по уровням стакана со стороны исполнения
        // (asks для BUY, bids для SELL), пока цена уровня не хуже нашего лимита
        // и пока есть остаток. Каждый уровень -- отдельный FillReport, чтобы
        // strategy.on_fill() видел реальные цены.
        const auto& levels = (it->side == Side::BUY) ? snap.asks : snap.bids;
        Quantity remaining = it->remaining_qty;
        bool any_fill = false;

        for (const auto& level : levels) {
            if (remaining == 0) break;
            const Price level_price = level.first;
            const Quantity level_qty = level.second;

            const bool crosses = (it->side == Side::BUY)
                                 ? (level_price <= it->price)
                                 : (level_price >= it->price);
            if (!crosses) break;

            Quantity fill_qty = std::min<Quantity>(remaining, level_qty);
            if (fill_qty == 0) continue;

            Price fill_price = (it->side == Side::BUY)
                ? static_cast<Price>(level_price * (1.0 + slippage_mult))
                : static_cast<Price>(level_price * (1.0 - slippage_mult));

            fills.push_back(FillReport{it->id, it->side, fill_price, fill_qty, now_us});

            const double notional = (static_cast<double>(fill_price) / 10000.0) *
                                    static_cast<double>(fill_qty);
            total_fees_ += notional * (cfg_.transaction_cost_bps / 10000.0);
            ++orders_filled_;

            remaining -= fill_qty;
            any_fill = true;
        }

        it->remaining_qty = remaining;
        if (any_fill && remaining == 0) {
            it = book_.erase(it);
        } else {
            ++it;
        }
    }

    return fills;
}

}  // namespace hft_backtest
