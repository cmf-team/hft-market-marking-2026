#include "stats.hpp"

#include <algorithm>
#include <cmath>

namespace hft {

BacktestStats::BacktestStats(double initial_cash_value)
    : initial_cash(initial_cash_value),
      cash(initial_cash_value),
      final_equity(initial_cash_value),
      peak_equity(initial_cash_value) {}

void BacktestStats::update_event_time(Timestamp ts) {
    if (first_event_ts == 0) {
        first_event_ts = ts;
    }
    last_event_ts = ts;
}

double BacktestStats::current_equity() const {
    return cash + position * last_mark_price;
}

void BacktestStats::update_equity_stats() {
    const double equity = current_equity();
    peak_equity = std::max(peak_equity, equity);
    max_drawdown = std::max(max_drawdown, peak_equity - equity);
}

void BacktestStats::on_book(const BookEvent& event) {
    ++book_events;
    ++total_events;
    update_event_time(event.ts);

    if (event.best_bid > 0.0 && event.best_ask > 0.0) {

        last_mark_price = (event.best_bid + event.best_ask) * 0.5;
        spread_sum += (event.best_ask - event.best_bid);
        ++spread_samples;
    }

    update_equity_stats();
}

void BacktestStats::on_trade(const TradeEvent& event) {
    ++trade_events;
    ++total_events;
    update_event_time(event.ts);

    if (last_mark_price <= 0.0 && event.price > 0.0) {
        last_mark_price = event.price;
    }

    update_equity_stats();
}

void BacktestStats::on_order_submitted() { ++submitted_orders; }

void BacktestStats::on_order_cancelled(std::size_t count) {
    cancelled_orders += count;
}

void BacktestStats::on_fill(const Fill& fill) {
    ++filled_orders;
    if (fill.maker) {
        ++maker_fills;
    } else {
        ++taker_fills;
    }

    if (fill.side == Side::Buy) {

        position += fill.qty;
        cash -= fill.qty * fill.price;
        buy_qty += fill.qty;
        buy_notional += fill.qty * fill.price;
    } else {

        position -= fill.qty;
        cash += fill.qty * fill.price;
        sell_qty += fill.qty;
        sell_notional += fill.qty * fill.price;
    }

    inventory_peak_abs = std::max(inventory_peak_abs, std::abs(position));
    update_event_time(fill.ts);

    if (last_mark_price <= 0.0 && fill.price > 0.0) {
        last_mark_price = fill.price;
    }

    update_equity_stats();
}

void BacktestStats::set_custom_feature(const std::string& key, double value) {
    custom_features[key] = value;
}

void BacktestStats::finalize(double fallback_mark_price) {
    if (last_mark_price <= 0.0 && fallback_mark_price > 0.0) {

        last_mark_price = fallback_mark_price;
    }

    final_equity = current_equity();
    total_pnl = final_equity - initial_cash;
    update_equity_stats();
}

double BacktestStats::fill_rate() const {
    if (submitted_orders == 0) {
        return 0.0;
    }
    return static_cast<double>(filled_orders) /
           static_cast<double>(submitted_orders);
}

double BacktestStats::avg_buy_price() const {
    if (buy_qty <= 0.0) {
        return 0.0;
    }
    return buy_notional / buy_qty;
}

double BacktestStats::avg_sell_price() const {
    if (sell_qty <= 0.0) {
        return 0.0;
    }
    return sell_notional / sell_qty;
}

double BacktestStats::avg_spread() const {
    if (spread_samples == 0) {
        return 0.0;
    }
    return spread_sum / static_cast<double>(spread_samples);
}

double BacktestStats::turnover() const {
    return buy_notional + sell_notional;
}

}
