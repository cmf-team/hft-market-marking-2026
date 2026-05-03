# HFT Market-Making Backtester -- Technical Design

## 1. Цели

Реализовать полностью контролируемый event-driven backtester для проверки
маркет-мейкинговых стратегий на исторических L2-данных и данных тиковых
сделок. Спецификация задания HFT-трека:

* симулировать LOB,
* размещать и отменять лимит-ордера,
* моделировать исполнение по правилу "когда рыночная цена пересекает
  уровень нашего ордера",
* считать PnL / inventory / turnover,
* реализовать Avellaneda–Stoikov (2008) и его расширение 2018 (microprice).

## 2. Источники данных

* `lob.csv`, формат tardis.dev: `index, local_timestamp,` затем 25 уровней
  по схеме `asks[i].price, asks[i].amount, bids[i].price, bids[i].amount`.
  Цены в исходном double; внутренне приводятся к `uint64_t` через
  `price * 10000` ("сотни тысячных", чтобы тики стали целочисленными и
  чтобы сравнения внутри `std::map` были устойчивыми).
* `trades.csv`: `index, local_timestamp, side, price, amount`. Используется
  для калибровки интенсивности fill λ(δ) (см. notebook).

`BacktestDataReader` грузит оба файла, сортирует по timestamp,
кэширует в `std::vector` для быстрых проходов.

## 3. Архитектура

```
            ┌────────────────────────────────────────┐
            │                                        │
   sample_data/                                      │
   lob.csv      ──► BacktestDataReader               │
   trades.csv                                        │
                            │                        │
                            ▼                        │
                     BacktestEngine                  │
                            │                        │
       ┌────────────────────┴───────────────────┐    │
       ▼                                        ▼    │
 IStrategy                                 ExecutionSimulator
  (AS-2008 / AS-2018 / Naive)              (resting orders + match)
       │                                        │
       └──────── on_market_data() ──────────────┤
                                                │
                              fills ◄────── match_against_book()
       │                                        │
       └──────── on_fill() ◄────────────────────┘
                            │
                            ▼
              summary.csv + timeseries.csv
```

### 3.1 IStrategy (`include/strategy_interface.h`)

* `on_market_data(snapshot, ts) -> StrategyAction { cancels, quotes }`
  -- стратегия видит новый снапшот L2 и решает, какие свои квоты убрать
  и какие выставить.
* `on_fill(FillReport)` -- движок сообщает о реальном исполнении;
  стратегия обновляет inventory/cash.
* `inventory()`, `cash()` -- доступ к состоянию для PnL.

### 3.2 ExecutionSimulator (`include/execution_simulator.h`)

Своя книга `std::list<RestingOrder>` со "своими" лимитами. На каждом
снапшоте walk-the-book matching: для каждого resting order проходим по
уровням противоположной стороны стакана сверху, пока цена уровня не хуже
нашего лимита и пока остался непокрытый объём.

```text
для каждого resting order:
    side_levels = (side==BUY) ? snap.asks : snap.bids
    remaining = order.qty
    for level in side_levels:
        if remaining == 0: break
        if (BUY and level.price > limit) or (SELL and level.price < limit): break
        fill_qty   = min(remaining, level.qty)
        fill_price = level.price (± slippage_bps)
        report FillReport
        fees += fill_price * fill_qty * transaction_cost_bps
        remaining -= fill_qty
```

Опция `queue_priority`: ордер не исполняется на том же снапшоте, в
котором был выставлен -- грубая модель FIFO-приоритета.

Это самая аккуратная "fair" модель в рамках того, что доступно из
L2-снапшотов. Полноценный matching против front-of-queue невозможен без
MBO/L3-данных.

### 3.3 AvellanedaStoikovStrategy (`include/avellaneda_stoikov_strategy.h`)

Базовый AS-2008. На каждом снапшоте:

1. **Reference price** s.
   * AS-2008: mid = (best_bid + best_ask) / 2.
   * AS-2018: microprice = (best_bid·ask_qty + best_ask·bid_qty) /
     (bid_qty + ask_qty). Реализовано в производном классе
     `AvellanedaStoikovMicroStrategy` через override `reference_price()`.
2. **Volatility σ**: rolling-окно из последних `sigma_window` приращений
   mid (`mid_t − mid_{t-1}`, ABSOLUTE, не log-return -- AS требует σ в
   ценовых единицах того же масштаба, что reference price s).
   σ² = sample variance.
3. **Time-to-horizon τ**: `1 - elapsed / T_seconds`, нижняя отсечка 1e-3.
4. **Reservation price** `r = s − q·γ·σ²·τ`, где q -- текущий inventory,
   γ -- параметр risk aversion.
5. **Optimal half-spread** `δ* = ½·γ·σ²·τ + (1/γ)·ln(1 + γ/k)`.
6. Котировки округляются к `tick_size_cents`: bid = floor(r − δ*), ask =
   ceil(r + δ*); если коллапсировали в одну точку -- ask сдвигается на
   один тик вверх.
7. Если |inventory| >= max_inventory -- сторона, увеличивающая риск, не
   выставляется (жёсткий cap).

Каждый снапшот стратегия запрашивает у движка `cancel_all + 2 quotes`,
имитируя классический "re-quote on every event" market-maker.

### 3.4 BacktestEngine

Главный цикл по `OrderBookSnapshot`:

```text
для каждого snapshot:
    fills = exec_.match_against_book(snap, ts)     # старые ордера
    apply_fills(fills, mark_price)                  # обновляем PnL
    action = strategy_.on_market_data(snap, ts)     # новые квоты
    exec_.apply_action(action, ts)
    обновить cash/inv/realized/unrealized PnL, max_drawdown, step_pnl
    каждые timeseries_step снапшотов -- writeln в timeseries CSV
```

PnL:
* realized = `cash − initial_cash − fees`,
* unrealized = `inventory · mid_mark`,
* total = realized + unrealized,
* max_drawdown = sup_t (peak_t − total_t).

Sharpe-per-step считается по приращениям total_pnl между снапшотами --
безразмерная sanity-метрика, не стандартная аннуализированная Sharpe;
честная аннуализация делается в notebook на минутных бинниках.

### 3.5 Конфиг (`include/backtest_config.h`)

Простой ini: `key = value`, секции в `[ ]` игнорируются, `;` и `#` --
комментарии. Без внешних зависимостей. Поддержанные ключи перечислены
в `src/backtest_config.cpp::apply()`. CLI-флаги в `main.cpp` имеют
приоритет над файлом.

## 4. Численные результаты

### 4.1 Sample dataset (5000 снапшотов)

Просто sanity-check, что pipeline работает.

| Стратегия | Orders placed | Orders filled | Final inv | Total PnL | Max DD |
|---|---|---|---|---|---|
| naive  | 1816 | 1  | 100 | -1e-4   | 0.010 |
| as2008 | 3472 | 48 | 0   | +0.0021 | 3e-4  |
| as2018 | 3472 | 48 | 0   | +0.0021 | 3e-4  |

Числа маленькие, потому что order_size=1 и выборка крошечная; на этом
горизонте microprice ≈ mid и AS-2008 ≈ AS-2018.

### 4.2 Full dataset (1.04M снапшотов, 22M трейдов)

Полные данные `MD/lob.csv` (925 МБ) + `MD/trades.csv` (946 МБ),
конфиги `configs/*_full.ini`, k=44.59 откалиброван
`scripts/calibrate_lambda.py` (4.17M валидных трейдов, 6 дней истории).

| Метрика | naive | AS-2008 | AS-2018 |
|---|---|---|---|
| Orders placed       | 1,033,506 | 1,190,782 | 1,190,782 |
| Orders cancelled    | 0         | 1,120,085 | 1,120,361 |
| Orders filled       | 1         | **72,476** | **72,231** |
| Total turnover ($)  | 1.10      | 706,158   | 703,827   |
| Total fees ($)      | 0.0001    | 70.6      | 70.4      |
| Realized PnL ($)    | −1.10     | +317.55   | +342.56   |
| Unrealized PnL ($)  | +0.77     | +86.01    | +52.45    |
| **Total PnL ($)**   | **−0.33** | **+403.56** | **+395.01** |
| Max drawdown ($)    | 0.53      | 46.98     | 47.63     |
| Sharpe per step     | −3e-4     | +6.7e-3   | **+7.4e-3** |
| Final inventory     | 100       | 11,170    | 6,812     |
| Max long inv        | 100       | 18,606    | 16,225    |
| Max short inv       | 0         | -10,830   | -11,822   |

AS-стратегии переигрывают naive на 3 порядка по PnL. AS-2018 имеет
чуть выше Sharpe и более узкий inventory range при сопоставимом
turnover -- microprice работает как expected.

### 4.3 Sweep по γ × order_size (AS-2008)

Запуск: `GAMMAS="0.01 0.05 0.1 0.5 1.0" ORDER_SIZES="100 1000 10000" \
STRATEGY=as2008 ./scripts/sweep_gamma.sh` (15 прогонов, 195 секунд).

| γ \ order_size | 100 | 1000 | 10000 |
|---|---|---|---|
| 0.01 | PnL=32, DD=14   | PnL=336,  DD=73 | **PnL=4011, DD=42** |
| 0.05 | PnL=34, DD=13   | PnL=333,  DD=65 | PnL=3404, DD=348 |
| 0.10 | PnL=34, DD=13   | PnL=321,  DD=39 | PnL=3241, DD=526 |
| 0.50 | PnL=33, DD=4.4  | PnL=341,  DD=42 | PnL=3276, DD=703 |
| 1.00 | PnL=41, **DD=0.48** | PnL=341, DD=67 | PnL=3186, DD=880 |

Наблюдения:

* PnL почти линейно растёт с `order_size` -- ожидаемо для market-making.
* Большое γ давит inventory: при γ=1 final inventory падает до 500
  единиц при order_size=100 (vs 8 500 при γ=0.01).
* Sweet spot risk-adjusted: γ=0.01, size=10 000 -- PnL/maxDD ≈ 95.
* "Безопасный" режим: γ=1, size=100 -- PnL/maxDD = 85, абсолютные числа
  маленькие, но просадка околонулевая.

## 5. Производительность

| Этап | Время | Заметка |
|---|---|---|
| Парсинг lob.csv (1.04M строк × 102 столбца) | ~25 с | std::stod в каждой ячейке |
| Парсинг trades.csv (22M строк) | ~5 мин | `--no-trades` пропускает |
| AS event loop (1.04M событий) | **1.2 с** | 870k events/s |
| Calibration `calibrate_lambda.py` | ~12 с | streaming pandas |
| Sweep 15 прогонов | 195 с | каждый ~13 с с `--no-trades` |
| Unit tests (32 ассерта) | < 1 мс | self-contained, без gtest |

## 5. Improvement roadmap

1. **Honest L2 matching.** Сейчас исполняется только против top-of-book.
   Расширить до проходов по нескольким уровням стакана при крупных
   ордерах ("walk-the-book" модель).
2. **Калибровка λ(δ) = A·exp(−k·δ) онлайн.** Сейчас A, k задаются в
   конфиге. Можно поддерживать скользящую оценку из проходящих
   `trades.csv`-сделок и динамически переоценивать половину спреда.
3. **Stoikov-2018 -- adverse-selection drift.** Помимо microprice как
   reference, добавить short-horizon prediction термины (OFI, queue
   imbalance) в reservation price.
4. **Adaptive γ.** Связать risk aversion с реализованной волатильностью
   и текущим inventory cap.
5. **Multiple instruments.** Сейчас один инструмент per run; вынести
   `InstrumentBookRegistry` в стиле `back-tester-2026`.
6. **Performance.** Заменить `std::list<RestingOrder>` на flat hash или
   intrusive list; векторизовать парсер CSV (mmap + SIMD).
7. **Тесты.** Подключить gtest и покрыть `ExecutionSimulator` (cross,
   no-cross, partial fill, slippage, queue priority) и
   `AvellanedaStoikovStrategy` (σ window, tau, inventory cap).
8. **Replay sanity.** Добавить дет-режим: фиксированный seed (хотя сейчас
   рандома нет), record/replay через сжатый бинарный формат.
