#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

#include "exchange.hpp"
#include "types.hpp"

namespace hft {


struct BacktestStats {
    explicit BacktestStats(double initial_cash_value);

    void on_book(const BookEvent& event);
    void on_trade(const TradeEvent& event);
    void on_order_submitted();
    void on_order_cancelled(std::size_t count = 1);
    void on_fill(const Fill& fill);
    void set_custom_feature(const std::string& key, double value);
    void finalize(double fallback_mark_price);


    double current_equity() const;
    double fill_rate() const;
    double avg_buy_price() const;
    double avg_sell_price() const;
    double avg_spread() const;
    double turnover() const;

    double initial_cash = 0.0;
    double cash = 0.0;
    double position = 0.0;
    double inventory_peak_abs = 0.0;
    double last_mark_price = 0.0;

    double final_equity = 0.0;
    double total_pnl = 0.0;
    double peak_equity = 0.0;
    double max_drawdown = 0.0;

    std::size_t total_events = 0;
    std::size_t book_events = 0;
    std::size_t trade_events = 0;

    std::size_t submitted_orders = 0;
    std::size_t cancelled_orders = 0;
    std::size_t filled_orders = 0;
    std::size_t maker_fills = 0;
    std::size_t taker_fills = 0;

    double buy_qty = 0.0;
    double sell_qty = 0.0;
    double buy_notional = 0.0;
    double sell_notional = 0.0;

    double spread_sum = 0.0;
    std::size_t spread_samples = 0;

    Timestamp first_event_ts = 0;
    Timestamp last_event_ts = 0;

    std::unordered_map<std::string, double> custom_features;

   private:

    void update_event_time(Timestamp ts);
    void update_equity_stats();
};

}
