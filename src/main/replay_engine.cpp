#include "replay_engine.hpp"

#include <chrono>
#include <stdexcept>
#include <thread>

namespace hft {

ReplayEngine::ReplayEngine(const BacktestConfig& config, IStrategy& strategy,
                           CsvLobReader& lob_reader, CsvTradeReader& trade_reader,
                           ExchangeEmulator& exchange, BacktestStats& stats)
    : config_(config),
      strategy_(strategy),
      lob_reader_(lob_reader),
      trade_reader_(trade_reader),
      exchange_(exchange),
      stats_(stats) {}

StrategyContext ReplayEngine::make_context(Timestamp now) const {
    StrategyContext context;
    context.now = now;
    context.position = stats_.position;
    context.cash = stats_.cash;
    context.mid_price = exchange_.mid_price();
    return context;
}

void ReplayEngine::maybe_sleep(Timestamp next_ts) {

    if (config_.replay_speed <= 0.0) {
        previous_ts_ = next_ts;
        return;
    }

    if (previous_ts_ < 0) {
        previous_ts_ = next_ts;
        return;
    }

    if (next_ts <= previous_ts_) {
        previous_ts_ = next_ts;
        return;
    }

    const auto delta_sim_us =
        static_cast<double>(next_ts - previous_ts_) / config_.replay_speed;
    if (delta_sim_us > 0.0) {
        const auto sleep_us = static_cast<std::int64_t>(delta_sim_us);

        if (sleep_us > 0 && sleep_us <= 5'000'000) {
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
        }
    }

    previous_ts_ = next_ts;
}

void ReplayEngine::handle_fills(const std::vector<Fill>& fills) {
    for (const Fill& fill : fills) {
        stats_.on_fill(fill);
        strategy_.on_fill(fill, exchange_, make_context(fill.ts));
    }
}

void ReplayEngine::apply_actions(const StrategyActions& actions, Timestamp now) {
    if (actions.cancel_all) {
        const std::size_t cancelled = exchange_.cancel_all();
        if (cancelled > 0) {
            stats_.on_order_cancelled(cancelled);
        }
    }

    for (std::uint64_t order_id : actions.cancel_order_ids) {
        if (exchange_.cancel_order(order_id)) {
            stats_.on_order_cancelled();
        }
    }

    for (const OrderRequest& request : actions.new_orders) {
        if (request.qty <= 0.0) {
            continue;
        }


        stats_.on_order_submitted();
        std::vector<Fill> fills;
        exchange_.submit_order(request, now, fills);
        handle_fills(fills);
    }
}

void ReplayEngine::run() {
    if (!lob_reader_.open()) {
        throw std::runtime_error("Failed to open LOB CSV file");
    }
    if (config_.include_trade_events && !trade_reader_.open()) {
        throw std::runtime_error("Failed to open trades CSV file");
    }

    BookEvent next_book;
    TradeEvent next_trade;
    bool has_book = lob_reader_.next(next_book);
    bool has_trade =
        config_.include_trade_events ? trade_reader_.next(next_trade) : false;

    strategy_.on_start(make_context(0));

    while (has_book || has_trade) {
        if (config_.max_total_events > 0 &&
            stats_.total_events >= config_.max_total_events) {
            break;
        }


        const bool take_book =
            has_book && (!has_trade || next_book.ts <= next_trade.ts);
        if (take_book) {
            maybe_sleep(next_book.ts);

            std::vector<Fill> fills;
            exchange_.on_book(next_book, fills);
            stats_.on_book(next_book);
            handle_fills(fills);
            apply_actions(
                strategy_.on_book(next_book, exchange_, make_context(next_book.ts)),
                next_book.ts);

            ++lob_processed_;
            if (config_.max_lob_events > 0 && lob_processed_ >= config_.max_lob_events) {
                has_book = false;
            } else {
                has_book = lob_reader_.next(next_book);
            }
        } else {
            maybe_sleep(next_trade.ts);

            std::vector<Fill> fills;
            exchange_.on_trade(next_trade, fills);
            stats_.on_trade(next_trade);
            handle_fills(fills);
            apply_actions(strategy_.on_trade(next_trade, exchange_,
                                             make_context(next_trade.ts)),
                          next_trade.ts);

            ++trade_processed_;
            if (config_.max_trade_events > 0 &&
                trade_processed_ >= config_.max_trade_events) {
                has_trade = false;
            } else {
                has_trade = trade_reader_.next(next_trade);
            }
        }
    }


    stats_.set_custom_feature("open_orders_end",
                              static_cast<double>(exchange_.open_order_count()));
    stats_.set_custom_feature("lob_rows_read",
                              static_cast<double>(lob_reader_.rows_read()));
    stats_.set_custom_feature("trade_rows_read",
                              static_cast<double>(trade_reader_.rows_read()));
    stats_.finalize(exchange_.mid_price());
}

}
