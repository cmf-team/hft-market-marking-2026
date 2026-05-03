#pragma once

#include "strategy/strategy_interface.hpp"
#include <deque>
#include <string>

namespace hft_backtest {

// Конфигурация стратегии. Параметры задокументированы в нотации Avellaneda &
// Stoikov (2008) "High-frequency trading in a limit order book".
struct AvellanedaStoikovConfig {
    // Risk aversion (gamma в paper): чем больше, тем сильнее стратегия
    // компенсирует inventory сдвигом reservation price от mid.
    double gamma = 0.1;

    // Order book intensity prefactor (k в paper): порядка скорости
    // экспоненциального затухания вероятности fill с расстоянием от mid.
    // Подбирается калибровкой по данным; для стартовых значений берём
    // 1.5 -- типичное эмпирическое значение для крипто-фьючерсов.
    double k = 1.5;

    // Горизонт сессии в секундах. Используется для вычисления остатка (T - t)
    // в формулах; на коротких бэктестах T = длина датасета.
    double T_seconds = 24 * 3600.0;

    // Окно (в обновлениях ордербука) для оценки sigma по rolling-вариации
    // log(mid_t / mid_{t-1}). Для стандартного AS sigma -- константа,
    // но мы её оцениваем онлайн, чтобы стратегия адаптировалась.
    std::size_t sigma_window = 200;

    // Размер котировки (одинаковый для bid и ask).
    double order_size = 1.0;

    // Минимальный шаг цены в "центах" (uint64_t). Котировки округляются вниз
    // (для ask -- вверх) до ближайшего шага.
    Price tick_size_cents = 1;

    // Использовать microprice вместо mid в качестве reference price (s).
    // Это и есть extension Stoikov-2018 (см. AvellanedaStoikovMicroStrategy).
    bool use_microprice = false;

    // Вкл/выкл inventory-skew (если false, котируем симметрично вокруг s --
    // упрощённый "constant spread" baseline для сравнения).
    bool enable_inventory_skew = true;

    // Жёсткий cap на inventory (в штуках). При |q| >= max_inventory стратегия
    // временно перестаёт котировать сторону, увеличивающую |q|.
    double max_inventory = 10.0;

    // Стартовый кэш для PnL-учёта.
    double initial_cash = 1'000'000.0;
};

// Базовый Avellaneda-Stoikov (2008).
//
// На каждом обновлении ордербука:
//   1) обновляем sigma по rolling-окну log-returns mid;
//   2) считаем tau = (T - t) в долях сессии (нормируем к 1);
//   3) reservation price r = s - q * gamma * sigma^2 * tau;
//   4) optimal half-spread delta* = (gamma * sigma^2 * tau) / 2
//                                 + (1 / gamma) * ln(1 + gamma / k);
//   5) котируем bid = floor(r - delta*) к шагу, ask = ceil(r + delta*).
//
// Производный класс AvellanedaStoikovMicroStrategy лишь меняет s на microprice.
class AvellanedaStoikovStrategy : public IStrategy {
public:
    explicit AvellanedaStoikovStrategy(const AvellanedaStoikovConfig& cfg);

    std::string name() const override;

    StrategyAction on_market_data(const OrderBookSnapshot& snapshot,
                                  uint64_t timestamp_us) override;

    void on_fill(const FillReport& fill) override;

    double inventory() const override { return inventory_; }
    double cash()      const override { return cash_; }

    // Доступ к внутренней телеметрии для отчёта.
    double last_sigma()       const { return last_sigma_; }
    double last_reservation() const { return last_reservation_; }
    double last_half_spread() const { return last_half_spread_; }
    double last_reference()   const { return last_reference_; }

protected:
    // Может быть переопределена потомком (microprice override).
    virtual double reference_price(const OrderBookSnapshot& snap) const;

    AvellanedaStoikovConfig cfg_;

private:
    void update_sigma(double mid);

    double inventory_ = 0.0;
    double cash_      = 0.0;

    // Состояние для оценки sigma: храним последние приращения mid
    // (абсолютные, в "центах"). См. update_sigma() / комментарий в .cpp.
    std::deque<double> log_returns_;
    double last_mid_ = 0.0;

    // Время старта (для вычисления tau).
    uint64_t start_ts_us_ = 0;

    // Последняя телеметрия.
    double last_sigma_       = 0.0;
    double last_reservation_ = 0.0;
    double last_half_spread_ = 0.0;
    double last_reference_   = 0.0;
};

// Стоиков-2018: то же самое, но reference price = microprice.
class AvellanedaStoikovMicroStrategy : public AvellanedaStoikovStrategy {
public:
    using AvellanedaStoikovStrategy::AvellanedaStoikovStrategy;
    std::string name() const override;
protected:
    double reference_price(const OrderBookSnapshot& snap) const override;
};

}  // namespace hft_backtest
