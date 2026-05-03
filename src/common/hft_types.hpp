#pragma once

// Минимальный набор базовых типов, на которых живёт весь backtester.
//
// Раньше эти определения тянулись из ../HFT_System/src/order_book.h, который
// был частью внешнего форка AbhigyanD/HFT_System. Чтобы репозиторий
// hft_backtest_integrated был полностью самодостаточным (collable из любого
// места без зависимостей), мы переписали нужные 7 строк сюда. Никакой логики
// HFT_System (OrderBook, MatchingEngine, RiskManager, GUI) мы не используем --
// её роль выполняет наш собственный ExecutionSimulator.

#include <cstdint>

// Сторона ордера. Используется в TradeData (история сделок) и нигде больше --
// внутри стратегий используется собственный enum hft_backtest::Side, чтобы не
// связывать API стратегии с этим типом.
enum class OrderSide : std::uint8_t { BUY = 0, SELL = 1 };

using OrderId   = std::uint64_t;
using Price     = std::uint64_t;  // цена в "тиках по 1/10000 USD" (см. price_to_cents)
using Quantity  = std::uint64_t;  // количество в штуках контракта
