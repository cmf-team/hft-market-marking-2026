#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <deque>
#include <unordered_map>
#include <vector>

#include "backtest/strategy/strategy.hpp"
#include "backtest/orders/limit_order.hpp"
#include "data_ingestion/data_types.hpp"

struct ASParams {
    double gamma         = 0.1;    // risk aversion coefficient (γ)
    double k             = 1.5;    // order arrival intensity
    double order_qty     = 1.0;    // order size in native units
    int    vol_window    = 100;    // rolling window size for σ estimation
    int    quote_every   = 1;      // refresh quotes every N LOB snapshots
    double max_inventory = 10.0;   // halt quoting if |q| exceeds this
    double session_secs  = 600.0;  // trading horizon T in seconds
    bool   verbose       = false;  // print quote decisions to stdout
};

class AvellanedaStoikovStrategy final : public strategy::IStrategy {
public:
    explicit AvellanedaStoikovStrategy(ASParams params = {})
        : params_(params) {
        assert(params_.k > 1e-9 && "k must be positive");
        assert(params_.gamma > 1e-9 && "gamma must be positive");
    }

    void set_order_submitter(strategy::SubmitOrderFn fn) override {
        submit_ = std::move(fn);
    }

    void set_order_canceller(strategy::CancelOrderFn fn) override {
        cancel_ = std::move(fn);
    }

    void on_trade(const data::Trade&) override {}

    void on_order_book(const data::OrderBookSnapshot& book) override {
        if (!initialized_) {
            session_start_ = book.local_timestamp;
            initialized_   = true;
        }

        if (book.bids[0].price == 0 || book.asks[0].price == 0) return;

        const double best_bid = book.bids[0].price * 1e-9;
        const double best_ask = book.asks[0].price * 1e-9;
        if (best_ask <= best_bid) return;

        const double mid = (best_bid + best_ask) * 0.5;
        last_ts_  = book.local_timestamp;

        update_sigma(mid);   // uses last_mid_ as the previous price
        last_mid_ = mid;     // update AFTER sigma computation

        ++lob_count_;
        if (lob_count_ % params_.quote_every != 0) return;
        if (sigma_ == 0.0) return;

        const double elapsed  = static_cast<double>(book.local_timestamp - session_start_);
        const double horizon  = params_.session_secs * 1e9;
        const double t        = elapsed / horizon;
        const double t_rem    = std::max(1.0 - t, 1e-6);

        const double sig2     = sigma_ * sigma_;
        const double r        = mid - inventory_ * params_.gamma * sig2 * t_rem;
        const double h        = (params_.gamma * sig2 * t_rem
                                 + (2.0 / params_.gamma) * std::log(1.0 + params_.gamma / params_.k))
                                * 0.5;

        const double bid_price = r - h;
        const double ask_price = r + h;

        if (bid_price <= 0.0 || ask_price <= bid_price) return;

        if (params_.verbose) {
            std::printf("[t=%.4f] mid=%.8f r=%.8f bid=%.8f ask=%.8f sigma=%.2e q=%.4f\n",
                        t, mid, r, bid_price, ask_price, sigma_, inventory_);
        }

        cancel_active_quotes();

        if (std::abs(inventory_) < params_.max_inventory) {
            submit_quote(backtest::Side::Buy,  bid_price, book.local_timestamp);
            submit_quote(backtest::Side::Sell, ask_price, book.local_timestamp);
        }
    }

    void on_order_update(const backtest::Order& order) override {
        using backtest::OrderStatus;

        if (order.status() == OrderStatus::Cancelled) {
            if (order.id() == active_bid_id_) active_bid_id_ = 0;
            if (order.id() == active_ask_id_) active_ask_id_ = 0;
            return;
        }
        if (order.status() != OrderStatus::Filled &&
            order.status() != OrderStatus::PartiallyFilled) return;

        // Delta fill to avoid double-counting on partial fills
        const uint64_t prev      = fill_seen_[order.id()];
        const uint64_t delta_sc  = order.filled_qty() - prev;
        fill_seen_[order.id()]   = order.filled_qty();

        const auto&  lo          = static_cast<const backtest::LimitOrder&>(order);
        const double fill_price  = lo.price() * 1e-9;
        const double fill_qty    = delta_sc  * 1e-9;

        if (order.side() == backtest::Side::Buy) {
            inventory_ += fill_qty;
            cash_      -= fill_price * fill_qty;
        } else {
            inventory_ -= fill_qty;
            cash_      += fill_price * fill_qty;
        }

        ++trade_count_;
        pnl_series_.push_back(cash_ + inventory_ * last_mid_);

        if (order.status() == OrderStatus::Filled) {
            if (order.id() == active_bid_id_) active_bid_id_ = 0;
            if (order.id() == active_ask_id_) active_ask_id_ = 0;
        }
    }

    strategy::Analytics calculate_analytics() const override {
        strategy::Analytics a;
        a.pnl    = cash_ + inventory_ * last_mid_;
        a.trades = trade_count_;

        if (pnl_series_.size() < 2) return a;

        std::vector<double> returns(pnl_series_.size() - 1);
        for (size_t i = 1; i < pnl_series_.size(); ++i)
            returns[i - 1] = pnl_series_[i] - pnl_series_[i - 1];

        double mean = 0.0;
        for (double r : returns) mean += r;
        mean /= static_cast<double>(returns.size());

        double var = 0.0;
        for (double r : returns) var += (r - mean) * (r - mean);
        var /= static_cast<double>(returns.size());

        a.sharpe = (var > 0.0) ? mean / std::sqrt(var) : 0.0;
        return a;
    }

private:
    ASParams params_;

    strategy::SubmitOrderFn submit_;
    strategy::CancelOrderFn cancel_;

    // Initialisation
    bool     initialized_   = false;
    uint64_t session_start_ = 0;
    uint64_t next_id_       = 1;
    uint64_t lob_count_     = 0;

    // Market state
    double   last_mid_ = 0.0;
    uint64_t last_ts_  = 0;

    // Volatility (rolling log-returns of mid-price)
    std::deque<double> log_returns_;
    double             sigma_ = 0.0;

    // Active quote IDs (0 = no live quote on that side)
    uint64_t active_bid_id_ = 0;
    uint64_t active_ask_id_ = 0;

    // Inventory and cash in native (unscaled) units
    double inventory_ = 0.0;
    double cash_      = 0.0;

    // Cumulative fill tracking per order to compute deltas
    std::unordered_map<uint64_t, uint64_t> fill_seen_;

    // Analytics accumulators
    uint64_t            trade_count_ = 0;
    std::vector<double> pnl_series_;

    // σ is computed as the sample std dev of absolute mid-price changes,
    // matching the AS model's arithmetic Brownian motion (dS = σ dW).
    void update_sigma(double new_mid) {
        if (last_mid_ > 0.0)
            log_returns_.push_back(new_mid - last_mid_);

        if (static_cast<int>(log_returns_.size()) > params_.vol_window)
            log_returns_.pop_front();

        if (static_cast<int>(log_returns_.size()) < 2) { sigma_ = 0.0; return; }

        double mean = 0.0;
        for (double r : log_returns_) mean += r;
        mean /= static_cast<double>(log_returns_.size());

        double var = 0.0;
        for (double r : log_returns_) var += (r - mean) * (r - mean);
        var /= static_cast<double>(log_returns_.size() - 1);

        sigma_ = std::sqrt(var);
    }

    void cancel_active_quotes() {
        if (active_bid_id_ != 0) { cancel_(active_bid_id_); active_bid_id_ = 0; }
        if (active_ask_id_ != 0) { cancel_(active_ask_id_); active_ask_id_ = 0; }
    }

    void submit_quote(backtest::Side side, double price, uint64_t ts) {
        const uint64_t price_sc = static_cast<uint64_t>(std::llround(price * 1e9));
        const uint64_t qty_sc   = static_cast<uint64_t>(std::llround(params_.order_qty * 1e9));
        const uint64_t id       = next_id_++;

        auto order = std::make_unique<backtest::LimitOrder>(id, ts, side, qty_sc, price_sc);
        submit_(std::move(order));

        if (side == backtest::Side::Buy)  active_bid_id_ = id;
        else                              active_ask_id_ = id;
    }
};