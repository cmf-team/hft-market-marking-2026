# HFT Backtest Engine

Событийно-управляемый бэктестер для высокочастотных торговых стратегий на C++20. Предназначен для тестирования стратегий на тиковых данных (tape data — сделки без стакана) с реалистичной симуляцией исполнения, учётом комиссий и полной аналитикой PnL.

---

## Содержание

- [Возможности](#возможности)
- [Архитектура](#архитектура)
- [Структура проекта](#структура-проекта)
- [Требования](#требования)
- [Сборка](#сборка)
- [Запуск](#запуск)
- [Входные данные](#входные-данные)
- [Написание стратегии](#написание-стратегии)
- [Встроенная стратегия: OFI Mean Reversion](#встроенная-стратегия-ofi-mean-reversion)
- [Отчёты](#отчёты)
- [Тесты](#тесты)
- [Производительность](#производительность)

---

## Возможности

- **Событийная архитектура** — воспроизведение тиков через `ReplayEngine` с поддержкой как максимальной скорости, так и замедленного режима с реальным таймингом
- **Шаблонная стратегия через C++20 Concepts** — стратегия задаётся compile-time, без виртуальных вызовов
- **Симуляция исполнения** — лимитные и рыночные ордера с price-cross критерием и расчётом комиссии в базисных пунктах
- **Фиксированная точность цен** — хранение цен в тиках (7 знаков после запятой) для исключения ошибок float
- **Lock-free кольцевой буфер** — `RingBuffer<T, N>` с capacity 2ⁿ для логирования сделок без heap-аллокаций в горячем пути
- **Полная аналитика PnL** — gross/net PnL, win rate, profit factor, max drawdown, Sharpe и Sortino ratios
- **Экспорт отчётов** — CSV, JSON, TXT и вывод в консоль
- **Режимы сборки** — Debug с ASan/UBSan, Release с LTO и -O3 -march=native, RelWithDebInfo
- **Тесты** — Catch2, подключаемый через CMake FetchContent

---

## Архитектура

```
                   ┌─────────────────┐
                   │  CsvDataLoader  │  Чтение и парсинг trades.csv
                   └────────┬────────┘
                            │ std::vector<MarketEvent>
                   ┌────────▼────────┐
                   │  ReplayEngine   │  Воспроизведение тиков, тайминг
                   └────────┬────────┘
                            │ on_init / on_event / on_finish
                   ┌────────▼────────┐
                   │    Strategy     │  Пользовательская стратегия (Concept)
                   │  (TestStrategy) │
                   └────┬───────┬────┘
                        │       │
              ┌─────────▼─┐ ┌───▼──────────┐
              │ Execution │ │  TradeLogger  │
              │  Engine   │ │ (RingBuffer)  │
              └───────────┘ └───────┬───────┘
                                    │
                         ┌──────────▼──────────┐
                         │    PnLCalculator     │
                         └──────────┬───────────┘
                                    │
                         ┌──────────▼──────────┐
                         │   ReportGenerator    │  CSV / JSON / TXT / Console
                         └─────────────────────┘
```

### Ключевые компоненты

| Компонент | Файл | Описание |
|---|---|---|
| `MarketEvent` | `market_event.hpp` | 32-байтовая структура тика, выровненная по 16 байт |
| `Order` / `OrderStatus` | `order.hpp` | Ордер с фабричными методами (`limit_buy`, `market_sell`, …) |
| `CsvDataLoader` | `csv_data_loader.*` | Загрузчик CSV с нулевыми аллокациями в парсере |
| `ReplayEngine<S>` | `replay_engine.hpp` | Шаблонный движок воспроизведения |
| `ExecutionEngine` | `execution_engine.hpp` | Price-cross исполнение + комиссия в bps |
| `RingBuffer<T,N>` | `ring_buffer.hpp` | Статический кольцевой буфер без аллокаций |
| `TradeLogger` | `trade_logger.hpp` | Логирование сделок в `RingBuffer` на 1M записей |
| `PnLCalculator` | `pnl_calculator.hpp` | Расчёт полного набора метрик эффективности |
| `ReportGenerator` | `report_generator.hpp` | Экспорт в консоль, CSV, JSON, TXT |
| `Strategy` concept | `strategy.hpp` | C++20-концепт, задающий интерфейс стратегии |

---

## Структура проекта

```
hft-backtest-engine/
├── CMakeLists.txt
├── CMakePresets.json
├── include/
│   └── backtest/
│       ├── market_event.hpp
│       ├── order.hpp
│       ├── strategy.hpp
│       ├── replay_engine.hpp
│       ├── execution_engine.hpp
│       ├── pnl_calculator.hpp
│       ├── ring_buffer.hpp
│       ├── trade_logger.hpp
│       ├── trade_record.hpp
│       ├── csv_data_loader.hpp
│       ├── report_generator.hpp
│       └── test_strategy.hpp
├── src/
│   ├── main.cpp
│   └── csv_data_loader.cpp
├── tests/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── test_execution.cpp
│   ├── test_ofi.cpp
│   └── test_replay.cpp
└── docs/
    ├── HFT.pdf
    └── test-strategy-description.md
```

---

## Требования

- **Компилятор**: GCC 11+ или Clang 14+ с поддержкой C++20
- **CMake**: 3.20+
- **Catch2**: подтягивается автоматически через FetchContent (нужен интернет при первой сборке)
- **OS**: Linux / macOS

---

## Сборка

### Debug (с Address Sanitizer + UBSan)

```bash
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug -j$(nproc)
```

### Release (с LTO + -march=native)

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release -j$(nproc)
```

### RelWithDebInfo

```bash
cmake -S . -B build/reldbg -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/reldbg -j$(nproc)
```

### Опции сборки

| Опция | По умолчанию | Описание |
|---|---|---|
| `BUILD_TESTS` | `ON` | Собрать unit-тесты с Catch2 |
| `BUILD_BENCHMARKS` | `OFF` | Собрать бенчмарки (Google Benchmark) |

Пример отключения тестов:

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF
```

---

## Запуск

Бинарник ожидает файл `MD/trades.csv` относительно рабочей директории:

```bash
# Из корня репозитория
./build/debug/hft-backtest-engine
```
или
```bash
# Из корня репозитория
./build/release/hft-backtest-engine
```

Пример вывода:

```
Loaded 543210 events
sizeof(MarketEvent): 32 bytes
alignof(MarketEvent): 16 bytes

Starting replay...
...
```

---

## Входные данные

Движок читает CSV-файл сделок (`trades.csv`) следующего формата:

```
index,timestamp_us,side,price,amount
0,1712000000000000,buy,150.2500000,100
1,1712000000001234,sell,150.2480000,50
```

- `timestamp_us` — UNIX-время в микросекундах
- `side` — `buy` / `sell` (регистронезависимо)
- `price` — цена с произвольным числом знаков после запятой (хранится в тиках с точностью 1e-7)
- `amount` — объём в лотах

Файл автоматически сортируется по `timestamp_us` после загрузки. Строки с ошибками парсинга пропускаются с предупреждением в stderr.

---

## Написание стратегии

Стратегия должна удовлетворять C++20-концепту `backtest::Strategy`:

```cpp
template<typename T>
concept Strategy = requires(T t, const MarketEvent& event) {
    { t.on_init()       } -> std::same_as<void>;
    { t.on_event(event) } -> std::same_as<void>;
    { t.on_finish()     } -> std::same_as<void>;
};
```

Минимальный пример:

```cpp
#include "backtest/strategy.hpp"
#include "backtest/execution_engine.hpp"
#include "backtest/trade_logger.hpp"

class MyStrategy {
public:
    void on_init() {
        engine_ = backtest::ExecutionEngine(10); // 10 bps комиссия
    }

    void on_event(const backtest::MarketEvent& event) {
        // Логика стратегии
        if (event.side == backtest::Side::Buy && position_ == 0) {
            auto order = backtest::Order::limit_buy(
                event.price_ticks, 100, ++next_order_id_, event.timestamp_us
            );
            pending_order_ = order;
        }

        if (pending_order_.isActive()) {
            auto report = engine_.checkLimitOrder(pending_order_, event);
            if (report.status == backtest::OrderStatus::Filled) {
                position_ = report.filled_qty;
                entry_price_ = report.avg_price;
                pending_order_.status = backtest::OrderStatus::Filled;
            }
        }
    }

    void on_finish() {
        std::cout << "Position: " << position_ << "\n";
    }

private:
    backtest::ExecutionEngine engine_{10};
    backtest::Order pending_order_{};
    int32_t position_ = 0;
    int64_t entry_price_ = 0;
    int64_t next_order_id_ = 0;
};
```

Подключение к `ReplayEngine`:

```cpp
#include "backtest/replay_engine.hpp"

MyStrategy strategy;
backtest::ReplayEngine engine(events, strategy, /*speed_multiplier=*/0.0);
engine.run();
```

`speed_multiplier = 0.0` — максимальная скорость без пауз; `1.0` — реальное время; `10.0` — в 10 раз быстрее реального.

---

## Встроенная стратегия: OFI Mean Reversion

`TestStrategy` (в `include/backtest/test_strategy.hpp`) реализует стратегию на основе **Order Flow Imbalance**:

$$\text{OFI} = \frac{V_{buy} - V_{sell}}{V_{buy} + V_{sell}} \in [-1, +1]$$

### Параметры

| Параметр | По умолчанию | Описание |
|---|---|---|
| `window_us` | 500 000 мкс | Скользящее окно для расчёта OFI |
| `imbalance_threshold` | 0.65 | Порог OFI для открытия лонга |
| `take_profit_bps` | 15 | Тейк-профит (+0.15%) |
| `stop_loss_bps` | 8 | Стоп-лосс (−0.08%) |
| `order_quantity` | 100 | Размер позиции в лотах |
| `commission_bps` | 10 | Комиссия на сторону |

### Логика работы

```
MarketEvent
    │
    ├─ updateOFI()          — обновить скользящее окно, выбросить устаревшие тики
    ├─ checkPendingOrders() — проверить исполнение / отмену лимитного ордера
    ├─ position == 0 → tryEnter()  — OFI ≥ threshold → лимитный buy
    └─ position > 0 → tryExit()   — +15 bps → take profit, −8 bps → stop loss
```

---

## Отчёты

После завершения прогона `ReportGenerator` предоставляет четыре формата вывода:

### Консоль

```cpp
backtest::ReportGenerator::printConsole(metrics, logger);
```

```
======================================================================
                         STRATEGY REPORT
======================================================================

PnL SUMMARY
----------------------------------------
  Gross PnL         :       0.123456 USD
  Commission        :       0.005000 USD
  Net PnL           :       0.118456 USD
  Max Drawdown      :       0.020000 USD

TRADE STATISTICS
----------------------------------------
  Total Trades      :            250
  Winning           :            162 (64.80%)
  ...

RISK METRICS
----------------------------------------
  Sharpe Ratio      :       1.234567
  Sortino Ratio     :       1.678900
  ...

  Verdict: PROFITABLE (Good risk-adjusted returns)
```

### CSV

```cpp
backtest::ReportGenerator::exportCsv("output/trades.csv", logger);
```

Поля: `trade_id, order_id, timestamp_us, side, quantity, exec_price, commission_ticks, pnl_ticks, expected_price, slippage_bps, exec_price_usd, pnl_usd, commission_usd`

### JSON

```cpp
backtest::ReportGenerator::exportJson("output/report.json", metrics, logger);
```

Содержит блоки `metadata`, `summary` и массив `trades`.

### TXT

```cpp
backtest::ReportGenerator::exportSummary("output/summary.txt", metrics);
```

Краткая текстовая сводка всех метрик.

---

## Тесты

```bash
# Сборка и запуск тестов
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug -j$(nproc)
cd build/debug
ctest --output-on-failure
```

Или напрямую:

```bash
./build/debug/tests/hft-tests
```

### Покрытие тестами

| Тест | Файл | Что тестируется |
|---|---|---|
| `[execution]` | `test_execution.cpp` | Исполнение лимитных/рыночных ордеров, расчёт комиссии, price-cross |
| `[ofi]` | `test_ofi.cpp` | Расчёт OFI: пустое окно, все buy/sell, смешанные, истечение окна |
| `[replay]` | `test_replay.cpp` | Воспроизведение 1000/100000 событий, тайминг в time-controlled режиме |

---

## Производительность

В режиме максимальной скорости (`time_multiplier = 0.0`) движок обрабатывает **100 000 событий менее чем за 100 мс** (проверяется в `test_replay.cpp`). 

- `MarketEvent` — 32 байта, `alignas(16)` для SIMD-friendly обхода
- `std::from_chars` в парсере — без heap-аллокаций, без locale
- `RingBuffer<T, N>` — статический буфер с power-of-2 capacity (битовый modulo), без динамической памяти в горячем пути
- Release-сборка: `-O3 -march=native` + LTO (межпроцедурная оптимизация)

В Debug-сборке автоматически включаются AddressSanitizer и UndefinedBehaviorSanitizer.
