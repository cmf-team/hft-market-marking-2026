#pragma once

#include "backtest/backtest_data_reader.hpp"
#include <memory>
#include <vector>

namespace hft_backtest {

// Простая стратегия пересечения цены
class SimpleCrossingStrategy {
public:
    SimpleCrossingStrategy(double initial_cash = 1000000.0);
    
    // Генерация торговых сигналов на основе order book
    struct TradingSignal {
        bool should_trade = false;
        bool is_buy = false;  // true = BUY, false = SELL
        double price = 0.0;
        double quantity = 100.0;  // Размер позиции
        std::string reason;
    };
    
    TradingSignal generate_signal(const OrderBookSnapshot& snapshot);
    
    // Обновление состояния стратегии
    void update_position(double position_change, double price);
    
    // Получение текущего состояния
    double get_current_position() const { return current_position_; }
    double get_current_cash() const { return current_cash_; }
    double get_entry_price() const { return entry_price_; }
    
private:
    [[maybe_unused]] double initial_cash_;
    double current_cash_;
    double current_position_;
    double entry_price_;
    
    // Параметры стратегии
    double position_size_ = 100.0;
    double spread_threshold_ = 0.01;  // 1 цент минимальный спред
    
    // История цен для принятия решений
    std::vector<double> price_history_;
    static const size_t MAX_HISTORY_SIZE = 10;
    
    // Вспомогательные методы
    double calculate_mid_price(const OrderBookSnapshot& snapshot) const;
    bool should_buy(const OrderBookSnapshot& snapshot) const;
    bool should_sell(const OrderBookSnapshot& snapshot) const;
};

} // namespace hft_backtest