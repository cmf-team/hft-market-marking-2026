# Technical Documentation

## 1. Objective

The framework replays historical market events and evaluates strategy behavior with a deterministic execution rule:

- order executes when market price crosses the order level

The implementation is focused on modularity and testability, so each subsystem can be replaced independently.

Design philosophy: make the "happy path" obvious first, then keep extension points clean.

## 2. System Architecture

### Data Layer

- `CsvLobReader`: streams top-of-book events from `lob.csv`
- `CsvTradeReader`: streams trades from `trades.csv`

Both readers parse only required columns and consume input row-by-row to avoid loading large files into memory.
This keeps memory stable even when source files are close to 1 GB.

### Replay Layer

- `ReplayEngine`: merges LOB and trade streams by timestamp
- supports optional time control via `replay_speed`
- dispatches events to exchange + strategy and applies generated actions

The replay loop is intentionally explicit (not heavily abstracted) so debugging is straightforward.

### Execution Layer

- `ExchangeEmulator`: maintains best bid/ask and open orders
- matches resting orders when book or trade price crosses order level
- supports order submission, cancellation, and cancel-all

Current model favors determinism over realism, which is useful for assignment-level evaluation.

### Strategy Layer

- `IStrategy`: event-driven interface
  - `on_book`
  - `on_trade`
  - `on_fill`
- `SimpleCycleStrategy`: baseline strategy used for smoke testing
- `AvellanedaStoikovStrategy`: inventory-aware market making from
  Avellaneda-Stoikov (2008), with an optional microprice fair value based on
  Stoikov (2018)

The interface is narrow on purpose, so new strategies can be added without touching engine internals.

### Analytics Layer

- `BacktestStats`: tracks:
  - PnL (cash, inventory, equity)
  - drawdown
  - execution stats (fills, fill rate, maker/taker, quantities, VWAP-like averages)
- custom features (`avg_spread`, rows read, open orders at end)
- turnover as total traded notional
- `report.cpp`: emits markdown report

Metrics are updated online during replay, so no second pass over event history is required.

## 3. Event Processing Sequence

1. Read next LOB and trade events
2. Select earliest timestamp event
3. Update exchange state and check fills
4. Update statistics
5. Call strategy callback
6. Apply strategy actions (cancel/submit)
7. Process immediate fills from newly submitted orders
8. Repeat until data ends or limits are reached

This sequence is deterministic given identical input data and config values.

## 4. Execution Model

### Cross Condition

- buy limit: fill if `market_price <= order_price`
- sell limit: fill if `market_price >= order_price`

`market_price` is taken from:

- best ask/bid on LOB updates
- trade price on trade updates

### Fill Price

Limit orders are filled at the order level (`order_price`) under this simplified model.
This choice avoids hidden slippage assumptions in baseline tests.

## 5. Configuration

`key=value` config supports:

- data paths
- event limits
- replay speed
- strategy parameters
- reporting path

See:

- `configs/sample_config.ini`
- `configs/cmf_config.ini`
- `configs/avellaneda_stoikov_sample_config.ini`
- `configs/microprice_as_sample_config.ini`
- `configs/microprice_as_cmf_config.ini`

Unknown keys are ignored by the parser, which helps keep configs backward compatible.
