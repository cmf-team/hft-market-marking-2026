# CMF HFT Market-Making Backtester

Реализация задания HFT-трека CMF: backtester исторических L2-данных с
семействами стратегий Avellaneda–Stoikov (2008) и его 2018 microprice-расширения.

Ключевые моменты:

- **C++20**, статическая сборка, без внешних рантайм-зависимостей кроме pthread.
- **Catch2 v3** для тестов (фетчится автоматически на первой сборке).
- **Walk-the-book matching** + partial fills + опциональная queue-priority +
  настраиваемые fees/slippage.
- **Avellaneda–Stoikov 2008** (mid reference) + **2018-microprice** через наследование.
- **Online σ** (rolling stddev по окну) и **online-калибровка `k`** через Python.
- **Sample dataset** на 5 000 LOB-снапшотов + 20 000 трейдов в `sample_data/` --
  smoke-test за 1 секунду из коробки.

## Структура

```
.
├── src/
│   ├── common/          BasicTypes.hpp (template) + hft_types.hpp
│   ├── strategy/        IStrategy + AvellanedaStoikov{,Micro}Strategy + Naive baseline
│   ├── backtest/        ExecutionSimulator + BacktestEngine + DataReader + Config
│   └── main/            CLI entry point
├── test/                Catch2-тесты (Execution, Strategy, EngineMetrics, BasicTypes)
├── config/              *.cfg для sample- и full-данных
├── sample_data/         lob_sample.csv, trades_sample.csv -- self-contained smoke tests
├── scripts/             calibrate_lambda.py, run_full.sh, sweep_gamma.sh, csv2feather.py
├── notebooks/
│   └── analysis.ipynb   equity curve, inventory, λ-fit
├── docs/
│   └── DESIGN.md        технический документ (математика + результаты + roadmap)
├── cmake/               GlobalSettings, ThirdPartyLibs, GenerateVersionFile (template)
├── CMakeLists.txt
└── README.md
```

## Сборка

Зависимости: `cmake >= 3.22`, C++20 toolchain (clang/g++). На первой сборке
автоматически фетчится Catch2 (~30 секунд).

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build -j
```

Артефакты:
- `build/bin/hft-market-making` -- backtest CLI
- `build/bin/test/hft-market-making-tests` -- Catch2-раннер

## Тесты

```bash
ctest --test-dir build --output-on-failure -j
```

11 unit-тестов: walk-the-book, partial fill, queue priority, cancel-all,
microprice tilt, AS inventory skew, finalize-метрики (fill_rate, calmar,
avg_trade_size, sharpe), zero-division safety.

## Запуск

### Smoke-test на sample-данных (1 сек)

```bash
mkdir -p reports
build/bin/hft-market-making config/as2008.cfg
build/bin/hft-market-making config/as2018.cfg
build/bin/hft-market-making config/naive.cfg
```

Ожидаемый вывод (AS-2008):

```
==== Backtest summary ====
  strategy                 AvellanedaStoikov-2008
  events                   5000
  orders_placed            3472
  orders_filled            48
  fill_rate                0.0138249
  avg_trade_size           1
  total_pnl                0.00214732
  max_drawdown             0.0003011
  calmar_ratio             7.13159
  sharpe_per_step          0.0529599
```

### CLI без конфига

```bash
build/bin/hft-market-making \
    --lob       sample_data/lob_sample.csv \
    --trades    sample_data/trades_sample.csv \
    --strategy  as2018 \
    --gamma     0.1 --k 1.5 \
    --order-size 1 \
    --summary   reports/run_summary.csv \
    --timeseries reports/run_timeseries.csv
```

`--strategy` принимает `naive | as2008 | as2018`. Полный список флагов:
`build/bin/hft-market-making --help`.

## Полный датасет

Положите `lob.csv` и `trades.csv` курса CMF в `MD/` корня репозитория и
запустите end-to-end pipeline:

```bash
./scripts/run_full.sh
```

Что произойдёт:
1. CMake-сборка релиз-бинаря.
2. `scripts/calibrate_lambda.py` -- калибрует λ(δ) = A·exp(-k·δ) на 22M
   трейдов (~12 секунд) и патчит `k = ...` в `config/as*_full.cfg`.
3. Прогон `naive`, `as2008`, `as2018` на полных данных (~5 минут каждый).
4. Сводная таблица в `reports/full_run_compare.csv`.

### Параметрический sweep

```bash
GAMMAS="0.01 0.05 0.1 0.5 1.0" \
ORDER_SIZES="100 1000 10000" \
STRATEGY=as2018 ./scripts/sweep_gamma.sh
```

Результат -- `reports/sweep/{strategy}_grid.csv` с метриками `total_pnl`,
`max_drawdown`, `fill_rate`, `calmar_ratio` для каждой точки сетки.

## Метрики

`reports/*_summary.csv` (одна строка на run):

| Метрика | Что значит |
|---|---|
| `realized_pnl`, `unrealized_pnl`, `total_pnl` | Прибыль закрытая / открытая по mark-to-market / суммарная |
| `final_inventory`, `max_long_inv`, `max_short_inv` | Текущая и пиковые позиции |
| `total_volume`, `total_turnover`, `total_fees` | Объём (шт), оборот ($), уплачено бирже |
| `orders_placed`, `orders_cancelled`, `orders_filled` | Активность стратегии |
| `fill_rate` | `orders_filled / orders_placed` -- съедают ли наши квоты |
| `avg_trade_size` | средний размер исполнения |
| `max_drawdown` | пик-к-дну equity-кривой ($) |
| `calmar_ratio` | `total_pnl / max_drawdown` -- profit / max боль |
| `sharpe_per_step` | mean/std step-PnL (для аннуализации см. notebook) |

`reports/*_timeseries.csv` (одна строка на `timeseries_step`-й снапшот):
`timestamp_us, mid, microprice, sigma, reservation, half_spread, inventory,
cash, total_pnl, fees, active_orders`.

`notebooks/analysis.ipynb` строит equity curve, inventory path,
котировочный vs realized spread и аннуализированные Sharpe / Sortino.

## Соответствие требованиям задания

| Требование | Реализация |
|---|---|
| Limit order book simulation | L2 × 25 уровней (`OrderBookSnapshot`) + резидентная книга своих квот в `ExecutionSimulator` |
| Limit order placement & cancellation | `IStrategy::on_market_data()` возвращает `cancels[]` + `quotes[]`; точечная и `cancel_all` |
| Order execution modeling | `match_against_book()`: BUY заполняется при `best_ask ≤ limit`, SELL -- при `best_bid ≥ limit`; walk-the-book |
| Partial fills | `min(remaining_qty, qty_на_встречном_уровне)`; остаток висит до следующего тика |
| PnL / inventory / turnover | См. таблицу метрик выше |
| Avellaneda–Stoikov (2008) | `AvellanedaStoikovStrategy`: r = s − qγσ²τ, δ* = ½γσ²τ + (1/γ)·ln(1+γ/k), σ-online |
| AS-2018 + microprice | `AvellanedaStoikovMicroStrategy`: тот же AS, но reference = (bid·askQty + ask·bidQty)/(bidQty+askQty) |
| Execution = market crosses the order level | Реализовано буквально как в задании |
| Sample dataset & configs | `sample_data/` + `config/*.cfg` |
| Performance report | `*_summary.csv` + `*_timeseries.csv` + `notebooks/analysis.ipynb` |
| Technical documentation | `docs/DESIGN.md` |

## Roadmap

См. `docs/DESIGN.md` раздел "Improvement roadmap". Кратко:
- queue-position model per-order (вместо боолевого `queue_priority`),
- min/max spread clamp,
- liquidate_on_finish для чистого PnL,
- quote throttling по `quote_interval_us`,
- multi-instrument backtest (одна стратегия на корзину символов).
