#pragma once

#include "backtest/backtest_data_reader.hpp"
#include "strategy/strategy_interface.hpp"
#include <cstdint>
#include <list>
#include <vector>

namespace hft_backtest {

struct ExecutionConfig {
    double transaction_cost_bps = 1.0;   // комиссия в б.п. от notional
    double slippage_bps         = 0.0;   // дополнительный сдвиг исполнения
    bool   queue_priority       = false; // если true, ордер не исполняется на
                                         // том же снапшоте, в котором был
                                         // выставлен (модель приоритета FIFO).
};

// Лимит-ордер, который "висит" в книге и ждёт пересечения.
struct RestingOrder {
    OrderId  id;
    Side     side;
    Price    price;          // лимит-цена в центах
    Quantity remaining_qty;
    uint64_t placed_ts_us;
};

// Симулятор исполнения "по пересечению цены".
//
// Правило исполнения (минимально-достаточное для Avellaneda-Stoikov,
// заведомо консервативное):
//
//   * BUY-ордер заполняется на цену min(limit, best_ask),
//     если на текущем снапшоте best_ask <= limit и на ордере уже истёк
//     "тик приоритета" (см. queue_priority).
//   * SELL-ордер заполняется на цену max(limit, best_bid),
//     если best_bid >= limit на текущем снапшоте.
//
// Размер исполнения = min(remaining_qty, qty_на_противоположной_лучшей_цене),
// чтобы хотя бы грубо моделировать ёмкость рынка.
//
// Почему этого достаточно:
//   - В исходных данных у нас L2-снапшоты, без MBO, поэтому моделировать
//     честный matching по очереди невозможно.
//   - Для market-making оценка по "пересекли -> заполнили" -- стандартный
//     baseline в литературе (Avellaneda-Stoikov, Cartea-Jaimungal).
class ExecutionSimulator {
public:
    explicit ExecutionSimulator(const ExecutionConfig& cfg = ExecutionConfig{});

    // Принять решение стратегии: отменить старые квоты и поставить новые.
    // Возвращает фактически выставленные ордера (с присвоенными id).
    std::vector<RestingOrder> apply_action(const StrategyAction& action,
                                           uint64_t now_us);

    // На каждом снапшоте: попытаться исполнить активные лимит-ордера.
    // Возвращает список исполнений (для стратегии и для PnL-учёта).
    std::vector<FillReport> match_against_book(const OrderBookSnapshot& snap,
                                               uint64_t now_us);

    // Метрики для отчёта.
    uint64_t orders_placed()    const { return orders_placed_; }
    uint64_t orders_cancelled() const { return orders_cancelled_; }
    uint64_t orders_filled()    const { return orders_filled_; }
    double   total_fees()       const { return total_fees_; }

    // Активные ордера (для дебага / визуализации).
    const std::list<RestingOrder>& active_orders() const { return book_; }

private:
    ExecutionConfig cfg_;
    std::list<RestingOrder> book_;
    OrderId next_order_id_ = 1;

    uint64_t orders_placed_    = 0;
    uint64_t orders_cancelled_ = 0;
    uint64_t orders_filled_    = 0;
    double   total_fees_       = 0.0;
};

}  // namespace hft_backtest
