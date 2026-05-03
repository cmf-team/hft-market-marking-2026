#pragma once

#include "backtest/backtest_data_reader.hpp"
#include "common/hft_types.hpp"
#include <memory>
#include <string>
#include <vector>

namespace hft_backtest {

// Сторона котировки стратегии. Не путать с OrderSide -- собственный enum,
// чтобы API стратегии не было привязано к деталям нижележащих типов.
enum class Side : uint8_t { BUY = 0, SELL = 1 };

// Заявка на размещение лимит-ордера, которую возвращает стратегия.
// Цена -- в "центах" (uint64_t, *price_to_cents). Quantity -- в штуках.
struct QuoteRequest {
    Side  side;
    Price price;
    Quantity quantity;
};

// Действие отмены: либо одной заявки по id, либо всего своего стека.
struct CancelRequest {
    bool cancel_all = false;
    OrderId order_id = 0;  // используется если cancel_all == false
};

// Что стратегия возвращает движку на каждом event-тике.
struct StrategyAction {
    std::vector<CancelRequest> cancels;
    std::vector<QuoteRequest>  quotes;
};

// Информация об исполнении, которую движок возвращает стратегии,
// чтобы она могла обновить inventory / cash / собственное PnL.
struct FillReport {
    OrderId   order_id;
    Side      side;
    Price     price;       // цена, по которой реально исполнили
    Quantity  quantity;
    uint64_t  timestamp_us;
};

class IStrategy {
public:
    virtual ~IStrategy() = default;

    // Имя для логов / отчётов.
    virtual std::string name() const = 0;

    // Вызывается на каждом обновлении ордербука.
    // Стратегия может попросить отменить старые квоты и поставить новые.
    virtual StrategyAction on_market_data(const OrderBookSnapshot& snapshot,
                                          uint64_t timestamp_us) = 0;

    // Колбэк об исполнении -- стратегия двигает inventory / cash.
    virtual void on_fill(const FillReport& fill) = 0;

    // Текущая позиция (положительная = long, отрицательная = short).
    virtual double inventory() const = 0;

    // Реализованный кэш-баланс (без учёта unrealized).
    virtual double cash() const = 0;
};

}  // namespace hft_backtest
