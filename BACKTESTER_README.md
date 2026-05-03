# HFT market-making backtester patch

This patch turns `hft-market-making` into a CSV event backtester that merges:

- `lob1.csv` LOB snapshots by `local_timestamp`
- `Trades1.csv` executed trades by `local_timestamp`

The executable processes events chronologically. When the strategy has active market-making limit orders, fills can happen by:

- `--fill-mode overlap`: LOB overlap OR trade overlap, as requested
- `--fill-mode trade_only`: only actual trade events fill orders

## Build

Use the existing MSVC build script from the project root, or configure with CMake:

```bat
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DBUILD_TESTS=OFF
cmake --build build --config Debug
```

## Run

```bat
build\bin\Debug\hft-market-making.exe ^
  --lob S:\YandexDisk\CMF\lob1.csv ^
  --trades S:\YandexDisk\CMF\Trades1.csv ^
  --out S:\YandexDisk\CMF\closed_trades.csv ^
  --center reservation ^
  --fill-mode overlap ^
  --volume 1 ^
  --quote-half-spread-ticks 1 ^
  --tp-ticks 2 ^
  --sl-ticks 4 ^
  --max-hold-us 3000000
```

## Output columns

The backtester saves one row per closed trade:

- enter_datetime
- enter_timestamp
- exit_timestamp
- exit_datetime
- rule_to_enter
- rule_to_exit: `stop_loss`, `take_profit`, `by_time_duration`, or `end_of_data`
- enter_price
- enter_direction
- enter_volume
- exit_price
- exit_direction
- exit_pnl

## Implemented strategy, first version

When flat, the bot quotes symmetrically around either:

- `mid`: raw mid price
- `reservation`: `mid_price - inventory * risk_aversion * volatility^2 * time_horizon`

In the current flat-entry version, inventory is zero at quote-placement time, so `reservation` equals `mid` before the first fill. After a fill, the bot cancels the opposite quote and manages the open position with TP / SL / max holding time.

The next patch should add Stoikov `P_micro`, OFI, trade-flow filters, and inventory-aware quote replacement while a position is open.
