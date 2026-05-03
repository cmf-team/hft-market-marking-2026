#pragma once

#include "backtest/strategy.hpp"
#include "backtest/market_event.hpp"
#include "backtest/order.hpp"
#include "backtest/execution_engine.hpp"
#include "backtest/trade_logger.hpp"
#include "backtest/pnl_calculator.hpp"
#include "backtest/report_generator.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <vector>

namespace backtest {

class TestStrategy : public BasicStrategy {
public:
    bool verbose = false;
    bool enable_trade_logging = true;
    TradeLogger trade_logger_;

    int64_t initial_capital_ticks = 100'000'000'000;
    int64_t commission_bps        = 10;
    int32_t order_quantity        = 100;

    int64_t window_us             = 500'000;
    double  imbalance_threshold   = 0.65;

    int64_t take_profit_bps       = 15;
    int64_t stop_loss_bps         = 8;

    static constexpr int64_t order_timeout_us       = 500'000;
    static constexpr int64_t limit_price_buffer_bps = 5;

    struct State {
        int64_t cash              = 0;
        int64_t position          = 0;
        int64_t entry_price       = 0;
        int64_t entry_commission  = 0;
        int64_t realized_pnl      = 0;
        int64_t max_drawdown      = 0;
        int64_t peak_equity       = 0;
        int64_t total_commission  = 0;

        size_t  trades_count      = 0;
        size_t  completed_round_trips = 0;
        size_t  winning_round_trips   = 0;
        size_t  buy_signals       = 0;
    };

    void on_init() noexcept {
        state_ = State{};
        state_.cash = initial_capital_ticks;
        state_.peak_equity = initial_capital_ticks;

        pending_orders_.clear();
        ofi_window_.clear();
        next_order_id_ = 1;
        next_trade_id_ = 1;

        if (enable_trade_logging) {
            trade_logger_.clear();
        }

        execution_engine_.setCommissionBps(commission_bps);
    }

    void on_event(const MarketEvent& event) noexcept {
        updateOFI(event);
        checkPendingOrders(event);

        if (state_.position == 0) {
            tryEnter(event);
        } else {
            tryExit(event);
        }

        updateDrawdown(event);
    }

    void on_finish() noexcept {
        double win_rate = (state_.completed_round_trips > 0)
            ? 100.0 * static_cast<double>(state_.winning_round_trips) /
                       static_cast<double>(state_.completed_round_trips)
            : 0.0;

        if (enable_trade_logging) {
            PnLCalculator calc;
            PnLMetrics metrics = calc.calculate(trade_logger_, initial_capital_ticks);

            ReportGenerator::printConsole(metrics, trade_logger_);

            ReportGenerator::exportCsv("output/trades.csv", trade_logger_);
            ReportGenerator::exportJson("output/report.json", metrics, trade_logger_);
            ReportGenerator::exportSummary("output/summary.txt", metrics);
        } else {
            std::cout << "\n=== Strategy Results (Quick) ===\n";
            std::cout << "Trades (legs):      " << state_.trades_count << "\n";
            std::cout << "Completed round-trips: " << state_.completed_round_trips << "\n";
            std::cout << "Win rate:           " << win_rate << " %\n";
            std::cout << "Buy signals:        " << state_.buy_signals << "\n";
            std::cout << "Commission (USD):   " << usd(state_.total_commission) << "\n";
            std::cout << "Realized PnL (USD): " << usd(state_.realized_pnl) << "\n";
            std::cout << "Max Drawdown (USD): " << usd(state_.max_drawdown) << "\n";
            std::cout << "==============================\n";
        }
    }

    [[nodiscard]] const State& getState() const noexcept { return state_; }
    [[nodiscard]] int64_t getRealizedPnl() const noexcept { return state_.realized_pnl; }
    [[nodiscard]] int64_t getMaxDrawdown()  const noexcept { return state_.max_drawdown; }
    [[nodiscard]] size_t  getTradesCount()  const noexcept { return state_.completed_round_trips; }

private:
    struct OFITick {
        int64_t timestamp_us;
        int64_t buy_volume;
        int64_t sell_volume;
    };

    void updateOFI(const MarketEvent& event) {
        while (!ofi_window_.empty() &&
               event.timestamp_us - ofi_window_.front().timestamp_us > window_us) {
            total_buy_volume_  -= ofi_window_.front().buy_volume;
            total_sell_volume_ -= ofi_window_.front().sell_volume;
            ofi_window_.pop_front();
        }

        int64_t bv = (event.side == Side::Buy)  ? event.amount : 0;
        int64_t sv = (event.side == Side::Sell) ? event.amount : 0;

        ofi_window_.push_back({event.timestamp_us, bv, sv});

        total_buy_volume_  += bv;
        total_sell_volume_ += sv;
    }

    [[nodiscard]] double getImbalance() const noexcept {
        int64_t total = total_buy_volume_ + total_sell_volume_;
        if (total == 0) return 0.0;
        return static_cast<double>(total_buy_volume_ - total_sell_volume_) /
               static_cast<double>(total);
    }

    void tryEnter(const MarketEvent& event) {
        if (!pending_orders_.empty()) return;

        double imb = getImbalance();

        if (imb >= imbalance_threshold) {
            ++state_.buy_signals;

            int64_t limit_price = event.price_ticks * (10'000 + limit_price_buffer_bps) / 10'000;

            Order order = Order::limit_buy(
                limit_price,
                order_quantity,
                next_order_id_++,
                event.timestamp_us
            );
            pending_orders_.push_back(order);
        }
    }

    void tryExit(const MarketEvent& event) noexcept {
        if (state_.entry_price == 0) return;

        int64_t change_bps = ((event.price_ticks - state_.entry_price) * 10'000)
                              / state_.entry_price;

        bool take_profit = (change_bps >= take_profit_bps);
        bool stop_loss   = (change_bps <= -stop_loss_bps);

        if (take_profit || stop_loss) {
            Order order = Order::market_sell(
                static_cast<int32_t>(state_.position),
                next_order_id_++,
                event.timestamp_us
            );

            ExecutionReport report = execution_engine_.executeMarketOrder(order, event);

            if (report.status == OrderStatus::Filled) {
                applyExit(report, event);
            }
        }
    }

    void checkPendingOrders(const MarketEvent& event) noexcept {
        for (auto& order : pending_orders_) {
            if (!order.isActive()) continue;

            if (state_.position > 0) {
                order.status = OrderStatus::Cancelled;
                continue;
            }

            if (event.timestamp_us - order.timestamp_us > order_timeout_us) {
                order.status = OrderStatus::Cancelled;
                continue;
            }

            ExecutionReport report = execution_engine_.checkLimitOrder(order, event);

            if (report.status == OrderStatus::Filled) {
                order.status = OrderStatus::Filled;
                order.filled_qty = report.filled_qty;
                applyEntry(report, event);
            }
        }

        pending_orders_.erase(
            std::remove_if(pending_orders_.begin(), pending_orders_.end(),
                [](const Order& o) { return !o.isActive(); }),
            pending_orders_.end()
        );
    }

    void applyEntry(const ExecutionReport& report, const MarketEvent& event) noexcept {
        state_.cash            -= report.filled_qty * report.avg_price + report.commission;
        state_.position        += report.filled_qty;
        state_.entry_price      = report.avg_price;
        state_.entry_commission = report.commission;
        state_.total_commission += report.commission;
        ++state_.trades_count;

        if (enable_trade_logging) {
            TradeRecord trade(
                next_trade_id_++,
                report.order_id,
                report.timestamp_us,
                OrderSide::Buy,
                report.filled_qty,
                report.avg_price,
                report.commission,
                0,
                0,
                event.price_ticks
            );
            trade_logger_.logTrade(trade);
        }

        if (verbose) {
            std::cout << "[ENTRY] price=" << report.avg_price
                      << " qty=" << report.filled_qty << "\n";
        }
    }

    void applyExit(const ExecutionReport& report, const MarketEvent& event) noexcept {
        int64_t gross_pnl = report.filled_qty * (report.avg_price - state_.entry_price);
        int64_t net_pnl   = gross_pnl - report.commission - state_.entry_commission;

        state_.cash            += report.filled_qty * report.avg_price - report.commission;
        state_.realized_pnl    += net_pnl;
        state_.total_commission += report.commission;

        ++state_.completed_round_trips;
        if (net_pnl > 0) {
            ++state_.winning_round_trips;
        }

        if (enable_trade_logging) {
            TradeRecord trade(
                next_trade_id_++,
                report.order_id,
                report.timestamp_us,
                OrderSide::Sell,
                report.filled_qty,
                report.avg_price,
                report.commission,
                state_.entry_price,
                net_pnl,
                event.price_ticks
            );
            trade_logger_.logTrade(trade);
        }

        if (verbose) {
            std::cout << "[EXIT]  price=" << report.avg_price
                      << " pnl=" << usd(net_pnl) << " USD\n";
        }

        state_.position         = 0;
        state_.entry_price      = 0;
        state_.entry_commission = 0;
        ++state_.trades_count;
    }

    void updateDrawdown(const MarketEvent& event) noexcept {
        int64_t equity = state_.cash + (state_.position * event.price_ticks);

        if (equity > state_.peak_equity) {
            state_.peak_equity = equity;
        } else {
            int64_t dd = state_.peak_equity - equity;
            if (dd > state_.max_drawdown) {
                state_.max_drawdown = dd;
            }
        }
    }

    [[nodiscard]] static double usd(int64_t ticks) noexcept {
        return static_cast<double>(ticks) / 10'000'000.0;
    }

    State state_;

    std::deque<OFITick> ofi_window_;
    mutable int64_t total_buy_volume_ = 0;
    mutable int64_t total_sell_volume_ = 0;

    std::vector<Order> pending_orders_;
    int64_t next_order_id_ = 1;

    int64_t next_trade_id_ = 1;

    ExecutionEngine execution_engine_;
};

}