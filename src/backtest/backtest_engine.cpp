#include "backtest/backtest_engine.hpp"
#include "strategy/avellaneda_stoikov_strategy.hpp"
#include "strategy/simple_crossing_strategy.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>

namespace hft_backtest {

namespace {

// Адаптер вокруг старой SimpleCrossingStrategy под новый IStrategy-интерфейс.
// Сохраняем её в репозитории как baseline для сравнения.
class NaiveCrossingAdapter : public IStrategy {
public:
    explicit NaiveCrossingAdapter(double initial_cash) : impl_(initial_cash) {}

    std::string name() const override { return "NaiveCrossing"; }

    StrategyAction on_market_data(const OrderBookSnapshot& snap, uint64_t /*ts*/) override {
        StrategyAction action;
        auto sig = impl_.generate_signal(snap);
        if (!sig.should_trade) return action;
        QuoteRequest q;
        q.side  = sig.is_buy ? Side::BUY : Side::SELL;
        q.price = static_cast<Price>(sig.price * 10000.0);
        q.quantity = static_cast<Quantity>(sig.quantity);
        action.quotes.push_back(q);
        return action;
    }

    void on_fill(const FillReport& fill) override {
        const double px = static_cast<double>(fill.price) / 10000.0;
        const double q  = static_cast<double>(fill.quantity);
        if (fill.side == Side::BUY) {
            impl_.update_position(+q, px);
        } else {
            impl_.update_position(-q, px);
        }
    }

    double inventory() const override { return impl_.get_current_position(); }
    double cash()      const override { return impl_.get_current_cash(); }

private:
    SimpleCrossingStrategy impl_;
};

}  // namespace

BacktestEngine::BacktestEngine(const BacktestConfig& cfg) : cfg_(cfg) {
    ExecutionConfig ecfg;
    ecfg.transaction_cost_bps = cfg_.transaction_cost_bps;
    ecfg.slippage_bps         = cfg_.slippage_bps;
    ecfg.queue_priority       = cfg_.queue_priority;
    exec_ = std::make_unique<ExecutionSimulator>(ecfg);
    build_strategy();
}

void BacktestEngine::build_strategy() {
    auto as_cfg = cfg_.as_cfg;
    if (as_cfg.initial_cash == 0.0) as_cfg.initial_cash = cfg_.initial_cash;
    switch (cfg_.strategy) {
        case StrategyKind::Naive:
            strategy_ = std::make_unique<NaiveCrossingAdapter>(cfg_.initial_cash);
            break;
        case StrategyKind::AvellanedaStoikov2008:
            strategy_ = std::make_unique<AvellanedaStoikovStrategy>(as_cfg);
            break;
        case StrategyKind::AvellanedaStoikov2018Micro:
            strategy_ = std::make_unique<AvellanedaStoikovMicroStrategy>(as_cfg);
            break;
    }
    summary_.strategy_name = strategy_->name();
}

bool BacktestEngine::load_data() {
    if (!reader_.load_order_book_data(cfg_.lob_path)) return false;
    if (cfg_.load_trades) {
        if (!reader_.load_trade_data(cfg_.trades_path)) return false;
    } else {
        std::cerr << "[backtest] skipping trades.csv (load_trades=false)\n";
    }
    return true;
}

void BacktestEngine::run() {
    const auto& snaps = reader_.get_order_book_snapshots();
    if (snaps.empty()) {
        std::cerr << "No order book snapshots loaded\n";
        return;
    }

    if (!cfg_.timeseries_csv.empty()) {
        timeseries_.open(cfg_.timeseries_csv);
        timeseries_ << "timestamp_us,mid,microprice,sigma,reservation,half_spread,"
                       "inventory,cash,total_pnl,fees,active_orders\n";
    }

    summary_.start_ts_us = snaps.front().timestamp_us;
    summary_.end_ts_us   = snaps.back().timestamp_us;

    const uint64_t total = snaps.size();
    for (uint64_t i = 0; i < total; ++i) {
        const auto& snap = snaps[i];
        if (snap.timestamp_us < cfg_.start_time_us) continue;
        if (snap.timestamp_us > cfg_.end_time_us) break;
        if (cfg_.max_events && events_seen_ >= cfg_.max_events) break;

        on_snapshot(snap, snap.timestamp_us);
        ++events_seen_;

        if (cfg_.print_progress && cfg_.progress_interval &&
            events_seen_ % cfg_.progress_interval == 0) {
            const double pct = 100.0 * static_cast<double>(i + 1) / static_cast<double>(total);
            std::cerr << "[backtest] " << std::fixed << std::setprecision(1) << pct
                      << "% events=" << events_seen_
                      << " inv=" << strategy_->inventory()
                      << " pnl=" << prev_total_pnl_
                      << " active=" << exec_->active_orders().size() << "\n";
        }
    }

    summary_.events_processed = events_seen_;
    if (timeseries_.is_open()) timeseries_.close();

    finalize_summary();
}

void BacktestEngine::finalize_summary() {
    summary_.final_inventory  = strategy_->inventory();
    summary_.orders_placed    = exec_->orders_placed();
    summary_.orders_cancelled = exec_->orders_cancelled();
    summary_.total_fees       = exec_->total_fees();

    if (summary_.orders_placed > 0) {
        summary_.fill_rate = static_cast<double>(summary_.orders_filled) /
                             static_cast<double>(summary_.orders_placed);
    }
    if (summary_.orders_filled > 0) {
        summary_.avg_trade_size = summary_.total_volume /
                                  static_cast<double>(summary_.orders_filled);
    }
    if (summary_.max_drawdown > 0.0) {
        summary_.calmar_ratio = summary_.pnl_total / summary_.max_drawdown;
    }
    if (!step_pnls_.empty()) {
        double sum = 0.0, sum2 = 0.0;
        for (double x : step_pnls_) { sum += x; sum2 += x * x; }
        const double n    = static_cast<double>(step_pnls_.size());
        const double mean = sum / n;
        const double var  = std::max(0.0, sum2 / n - mean * mean);
        const double sd   = std::sqrt(var);
        summary_.sharpe_per_step = (sd > 0.0) ? mean / sd : 0.0;
    }
}

void BacktestEngine::on_snapshot(const OrderBookSnapshot& snap, uint64_t ts) {
    // 1) исполняем сидящие лимит-ордера против новой картинки книги
    auto fills = exec_->match_against_book(snap, ts);

    // 2) считаем mark-price для PnL
    const double mark_px = (snap.bids.empty() || snap.asks.empty())
        ? 0.0
        : 0.5 * (static_cast<double>(snap.bids.front().first) +
                 static_cast<double>(snap.asks.front().first)) / 10000.0;

    apply_fills(fills, mark_px);

    // 3) спрашиваем у стратегии новые квоты
    auto action = strategy_->on_market_data(snap, ts);
    exec_->apply_action(action, ts);

    // 4) обновляем сводный PnL
    const double cash = strategy_->cash();
    const double inv  = strategy_->inventory();
    summary_.pnl_realized   = cash - cfg_.initial_cash - exec_->total_fees();
    summary_.pnl_unrealized = inv * mark_px;
    summary_.pnl_total      = summary_.pnl_realized + summary_.pnl_unrealized;

    // drawdown / step-pnl
    if (events_seen_ == 0) {
        prev_total_pnl_ = summary_.pnl_total;
        peak_total_pnl_ = summary_.pnl_total;
    } else {
        step_pnls_.push_back(summary_.pnl_total - prev_total_pnl_);
        prev_total_pnl_ = summary_.pnl_total;
        peak_total_pnl_ = std::max(peak_total_pnl_, summary_.pnl_total);
        summary_.max_drawdown = std::max(summary_.max_drawdown,
                                         peak_total_pnl_ - summary_.pnl_total);
    }
    summary_.max_long_inv  = std::max(summary_.max_long_inv,  inv);
    summary_.max_short_inv = std::min(summary_.max_short_inv, inv);

    // 5) timeseries-лог
    if (timeseries_.is_open() && cfg_.timeseries_step &&
        events_seen_ % cfg_.timeseries_step == 0) {
        log_timeseries_row(snap, ts);
    }
}

void BacktestEngine::apply_fills(const std::vector<FillReport>& fills, double mark_px) {
    (void)mark_px;
    for (const auto& f : fills) {
        strategy_->on_fill(f);
        const double px = static_cast<double>(f.price) / 10000.0;
        const double q  = static_cast<double>(f.quantity);
        summary_.total_volume   += q;
        summary_.total_turnover += px * q;
        summary_.orders_filled++;
    }
}

void BacktestEngine::log_timeseries_row(const OrderBookSnapshot& snap, uint64_t ts) {
    double mid = 0.0, micro = 0.0;
    if (!snap.bids.empty() && !snap.asks.empty()) {
        const double bid_px = static_cast<double>(snap.bids.front().first) / 10000.0;
        const double ask_px = static_cast<double>(snap.asks.front().first) / 10000.0;
        const double bid_q  = static_cast<double>(snap.bids.front().second);
        const double ask_q  = static_cast<double>(snap.asks.front().second);
        mid = 0.5 * (bid_px + ask_px);
        const double w = bid_q + ask_q;
        micro = (w > 0.0) ? (bid_px * ask_q + ask_px * bid_q) / w : mid;
    }

    double sigma = 0.0, r = 0.0, hs = 0.0;
    if (auto* as = dynamic_cast<AvellanedaStoikovStrategy*>(strategy_.get())) {
        sigma = as->last_sigma();
        r     = as->last_reservation() / 10000.0;
        hs    = as->last_half_spread() / 10000.0;
    }

    timeseries_ << ts << ',' << mid << ',' << micro << ',' << sigma << ','
                << r << ',' << hs << ','
                << strategy_->inventory() << ',' << strategy_->cash() << ','
                << summary_.pnl_total << ',' << exec_->total_fees() << ','
                << exec_->active_orders().size() << '\n';
}

void BacktestEngine::print_summary(std::ostream& os) const {
    auto fmt = [&](const std::string& k, auto v) {
        os << "  " << std::left << std::setw(24) << k << ' ' << v << '\n';
    };
    os << "==== Backtest summary ====\n";
    fmt("strategy",        summary_.strategy_name);
    fmt("events",          summary_.events_processed);
    fmt("orders_placed",   summary_.orders_placed);
    fmt("orders_cancelled",summary_.orders_cancelled);
    fmt("orders_filled",   summary_.orders_filled);
    fmt("fill_rate",       summary_.fill_rate);
    fmt("avg_trade_size",  summary_.avg_trade_size);
    fmt("total_volume",    summary_.total_volume);
    fmt("total_turnover",  summary_.total_turnover);
    fmt("total_fees",      summary_.total_fees);
    fmt("final_inventory", summary_.final_inventory);
    fmt("max_long_inv",    summary_.max_long_inv);
    fmt("max_short_inv",   summary_.max_short_inv);
    fmt("realized_pnl",    summary_.pnl_realized);
    fmt("unrealized_pnl",  summary_.pnl_unrealized);
    fmt("total_pnl",       summary_.pnl_total);
    fmt("max_drawdown",    summary_.max_drawdown);
    fmt("calmar_ratio",    summary_.calmar_ratio);
    fmt("sharpe_per_step", summary_.sharpe_per_step);
}

void BacktestEngine::export_summary(const std::string& path) const {
    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "Cannot open summary file: " << path << "\n";
        return;
    }
    f << "metric,value\n";
    f << "strategy,"          << summary_.strategy_name << '\n';
    f << "events,"            << summary_.events_processed << '\n';
    f << "orders_placed,"     << summary_.orders_placed << '\n';
    f << "orders_cancelled,"  << summary_.orders_cancelled << '\n';
    f << "orders_filled,"     << summary_.orders_filled << '\n';
    f << "fill_rate,"         << summary_.fill_rate << '\n';
    f << "avg_trade_size,"    << summary_.avg_trade_size << '\n';
    f << "total_volume,"      << summary_.total_volume << '\n';
    f << "total_turnover,"    << summary_.total_turnover << '\n';
    f << "total_fees,"        << summary_.total_fees << '\n';
    f << "final_inventory,"   << summary_.final_inventory << '\n';
    f << "max_long_inv,"      << summary_.max_long_inv << '\n';
    f << "max_short_inv,"     << summary_.max_short_inv << '\n';
    f << "realized_pnl,"      << summary_.pnl_realized << '\n';
    f << "unrealized_pnl,"    << summary_.pnl_unrealized << '\n';
    f << "total_pnl,"         << summary_.pnl_total << '\n';
    f << "max_drawdown,"      << summary_.max_drawdown << '\n';
    f << "calmar_ratio,"      << summary_.calmar_ratio << '\n';
    f << "sharpe_per_step,"   << summary_.sharpe_per_step << '\n';
}

}  // namespace hft_backtest
