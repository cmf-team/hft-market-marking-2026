#include "engine/backtester.hpp"

#include "exec/fill.hpp"

#include <vector>

namespace cmf {

Backtester::Backtester(const std::string& book_path, const std::string& trades_path)
    : stream_(book_path, trades_path) {}

void Backtester::run(StrategyBase* strategy, MatchingEngine* me, PnLTracker* pnl) {
    std::vector<Fill> fills;
    fills.reserve(4);

    while (stream_.next()) {
        last_ts_ = stream_.current_ts();

        if (stream_.current_type() == EventStream::Type::BookUpdate) {
            const auto& book = stream_.book();
            if (strategy) strategy->on_book_update(book);
            if (pnl && !book.empty()) {
                pnl->mark_to_market(book.timestamp(), book.mid_price());
            }
        } else {
            const auto& trade = stream_.trade();
            if (strategy) strategy->on_trade(trade);

            if (me) {
                fills.clear();
                me->on_trade(trade, fills);
                for (const auto& f : fills) {
                    if (pnl)      pnl->apply_fill(f);
                    if (strategy) strategy->on_fill(f);
                }
            }
        }
    }
}

}  // namespace cmf
