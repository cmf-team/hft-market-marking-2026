#include "engine/backtester.hpp"

#include "exec/fill.hpp"

#include <filesystem>
#include <fstream>
#include <vector>

namespace cmf {

Backtester::Backtester(const std::string& book_path, const std::string& trades_path)
    : stream_(book_path, trades_path) {}

void Backtester::enable_csv_output(const std::string& path, std::size_t sample_every) {
    csv_path_         = path;
    csv_sample_every_ = sample_every;
}

void Backtester::run(StrategyBase* strategy, MatchingEngine* me, PnLTracker* pnl) {
    std::vector<Fill> fills;
    fills.reserve(4);

    std::ofstream csv_out;
    if (!csv_path_.empty()) {
        const auto dir = std::filesystem::path(csv_path_).parent_path();
        if (!dir.empty())
            std::filesystem::create_directories(dir);
        csv_out.open(csv_path_);
        csv_out << "timestamp_ns,mid_price,equity,position,cash,fill_count,volume\n";
    }

    std::size_t book_n = 0;

    while (stream_.next()) {
        last_ts_ = stream_.current_ts();

        if (stream_.current_type() == EventStream::Type::BookUpdate) {
            const auto& book = stream_.book();
            if (strategy) strategy->on_book_update(book);
            if (pnl && !book.empty()) {
                pnl->mark_to_market(book.timestamp(), book.mid_price());
                if (csv_out.is_open() && (++book_n % csv_sample_every_ == 0)) {
                    csv_out << pnl->last_ts()    << ','
                            << pnl->last_mid()   << ','
                            << pnl->equity()     << ','
                            << pnl->position()   << ','
                            << pnl->cash()       << ','
                            << pnl->fill_count() << ','
                            << pnl->volume()     << '\n';
                }
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
