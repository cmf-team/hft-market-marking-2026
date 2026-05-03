#include "strategy/simple_crossing_strategy.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

// Печать "сигналов" в stdout оставлена для CLI-демо. На full-run может быть
// шумно: можно отключить через -DSIMPLE_STRATEGY_SILENT.
#ifdef SIMPLE_STRATEGY_SILENT
#  define SCS_LOG(x) ((void)0)
#else
#  define SCS_LOG(x) do { x; } while (0)
#endif

namespace hft_backtest {

SimpleCrossingStrategy::SimpleCrossingStrategy(double initial_cash) 
    : initial_cash_(initial_cash), current_cash_(initial_cash), 
      current_position_(0.0), entry_price_(0.0) {
}

SimpleCrossingStrategy::TradingSignal SimpleCrossingStrategy::generate_signal(const OrderBookSnapshot& snapshot) {
    TradingSignal signal;
    
    if (snapshot.bids.empty() || snapshot.asks.empty()) {
        return signal;  // Нет данных для торговли
    }
    
    double mid_price = calculate_mid_price(snapshot);
    if (mid_price <= 0) {
        return signal;
    }
    
    // Добавляем цену в историю
    price_history_.push_back(mid_price);
    if (price_history_.size() > MAX_HISTORY_SIZE) {
        price_history_.erase(price_history_.begin());
    }
    
    // Нужно минимум 3 цены для принятия решения
    if (price_history_.size() < 3) {
        return signal;
    }
    
    // Простая логика пересечения цены
    if (current_position_ == 0.0) {
        // Нет позиции - ищем точку входа
        if (should_buy(snapshot)) {
            signal.should_trade = true;
            signal.is_buy = true;
            signal.price = snapshot.asks[0].first / 100.0;  // Покупаем по ask
            signal.quantity = position_size_;
            signal.reason = "Price crossing - BUY signal";
            
            SCS_LOG(std::cout << "[naive] BUY " << signal.quantity
                                << " @ $" << std::fixed << std::setprecision(4) << signal.price
                                << " - " << signal.reason << "\n");
        }
    } else if (current_position_ > 0.0) {
        // Есть длинная позиция - ищем точку выхода
        if (should_sell(snapshot)) {
            signal.should_trade = true;
            signal.is_buy = false;
            signal.price = snapshot.bids[0].first / 100.0;  // Продаем по bid
            signal.quantity = std::abs(current_position_);
            signal.reason = "Price crossing - SELL signal (exit long)";
            
            SCS_LOG(std::cout << "[naive] SELL " << signal.quantity
                                << " @ $" << std::fixed << std::setprecision(4) << signal.price
                                << " - " << signal.reason << "\n");
        }
    }
    
    return signal;
}

void SimpleCrossingStrategy::update_position(double position_change, double price) {
    current_position_ += position_change;
    
    if (position_change > 0) {
        current_cash_ -= position_change * price;
        entry_price_ = price;
        SCS_LOG(std::cout << "[naive] +" << position_change << " @ $" << price
                            << " | cash=" << current_cash_ << "\n");
    } else {
        current_cash_ -= position_change * price;
        SCS_LOG({
            const double pnl = (price - entry_price_) * std::abs(position_change);
            std::cout << "[naive] " << position_change << " @ $" << price
                      << " | pnl=" << pnl << " | cash=" << current_cash_ << "\n";
        });
    }
}

double SimpleCrossingStrategy::calculate_mid_price(const OrderBookSnapshot& snapshot) const {
    if (snapshot.bids.empty() || snapshot.asks.empty()) {
        return 0.0;
    }
    
    double bid = snapshot.bids[0].first / 100.0;  // Конвертируем из центов
    double ask = snapshot.asks[0].first / 100.0;
    
    return (bid + ask) / 2.0;
}

bool SimpleCrossingStrategy::should_buy(const OrderBookSnapshot& snapshot) const {
    if (price_history_.size() < 3) {
        return false;
    }
    
    // Простая логика: покупаем если цена падала и начинает расти
    double current_price = calculate_mid_price(snapshot);
    double prev_price = price_history_[price_history_.size() - 2];
    double prev_prev_price = price_history_[price_history_.size() - 3];
    
    // Проверяем спред - не торгуем если спред слишком широкий
    double spread = (snapshot.asks[0].first - snapshot.bids[0].first) / 100.0;
    if (spread > spread_threshold_) {
        return false;
    }
    
    // Паттерн: цена падала, теперь растет
    bool was_falling = prev_price < prev_prev_price;
    bool now_rising = current_price > prev_price;
    
    // Минимальное изменение цены для фильтрации шума
    double min_change = 0.001;  // 0.1 цента
    bool significant_change = std::abs(current_price - prev_price) > min_change;
    
    return was_falling && now_rising && significant_change;
}

bool SimpleCrossingStrategy::should_sell(const OrderBookSnapshot& snapshot) const {
    if (price_history_.size() < 3 || entry_price_ <= 0) {
        return false;
    }
    
    double current_price = calculate_mid_price(snapshot);
    double prev_price = price_history_[price_history_.size() - 2];
    double prev_prev_price = price_history_[price_history_.size() - 3];
    
    // Проверяем спред
    double spread = (snapshot.asks[0].first - snapshot.bids[0].first) / 100.0;
    if (spread > spread_threshold_) {
        return false;
    }
    
    // Паттерн: цена росла, теперь падает
    bool was_rising = prev_price > prev_prev_price;
    bool now_falling = current_price < prev_price;
    
    // Минимальное изменение цены
    double min_change = 0.001;
    bool significant_change = std::abs(current_price - prev_price) > min_change;
    
    // Также продаем если прибыль достигла 0.5% или убыток 0.2%
    double pnl_pct = (current_price - entry_price_) / entry_price_ * 100.0;
    bool take_profit = pnl_pct > 0.5;
    bool stop_loss = pnl_pct < -0.2;
    
    return (was_rising && now_falling && significant_change) || take_profit || stop_loss;
}

} // namespace hft_backtest