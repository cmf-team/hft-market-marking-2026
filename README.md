# CMF HFT Market-Making Backtester

This repository contains a compact C++ backtesting engine for the first CMF HFT
technical assignment. It replays historical limit order book and trade CSV data,
simulates limit-order placement/cancellation/execution, and reports strategy
performance.

## Assignment Coverage

- Limit order book replay from CSV.
- Limit order placement and cancellation.
- Deterministic execution model: orders fill when market price crosses the order
  level.
- Metrics: PnL, inventory, turnover, fills, fill rate, drawdown, and spread.
- Strategies:
  - simple cycle baseline;
  - Avellaneda-Stoikov (2008);
  - microprice-enhanced Avellaneda-Stoikov using top-of-book imbalance inspired
    by Stoikov (2018).
- Deliverables:
  - sample dataset and configs;
  - full-data configs;
  - markdown performance reports;
  - technical documentation, model description, and improvement roadmap.

The full assignment data was provided at:
https://drive.google.com/file/d/1DiP5arvCEMxLHZ0R2mAS4lcMjnPHSrEJ

The large full-data files are expected locally as `lob.csv` and `trades.csv`.
They are intentionally not committed. Small sample slices are committed under
`data/`.

## Project Layout

- `src/main/`: backtester source code and strategy implementations.
- `src/common/`: original framework common module.
- `configs/`: runnable strategy configs.
- `data/`: sample CSV slices for quick smoke tests.
- `docs/technical_documentation.md`: architecture and execution model.
- `docs/model_description.md`: Avellaneda-Stoikov and microprice formulas.
- `docs/performance_results.md`: full and sample experiment tables.
- `docs/improvement_roadmap.md`: next steps.
- `reports/`: generated markdown reports.

## Build

Linux:

```bash
sudo apt install -y cmake g++ clang-format
cmake -B build -S . -DBUILD_TESTS=OFF
cmake --build build -j
```

Windows with Visual Studio developer tools:

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -G Ninja -S . -B build -DBUILD_TESTS=OFF && "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build'
```

Executable:

- Linux/macOS: `build/bin/hft-market-making`
- Windows: `build/bin/hft-market-making.exe`

## Run

Sample runs:

```bash
build/bin/hft-market-making configs/sample_config.ini
build/bin/hft-market-making configs/avellaneda_stoikov_sample_config.ini
build/bin/hft-market-making configs/microprice_as_sample_config.ini
```

Full-data runs after placing `lob.csv` and `trades.csv` in the repository root:

```bash
build/bin/hft-market-making configs/cmf_config.ini
build/bin/hft-market-making configs/avellaneda_stoikov_cmf_config.ini
build/bin/hft-market-making configs/microprice_as_cmf_config.ini
```

Each run prints a report to stdout and writes it to the config's `report_path`.

## Strategy Notes

The Avellaneda-Stoikov strategy quotes around an inventory-adjusted reservation
price:

```text
r = s - q * gamma * sigma^2 * tau
spread = gamma * sigma^2 * tau + (2 / gamma) * log(1 + gamma / k)
```

The microprice variant replaces `s` with:

```text
I = bid_qty / (bid_qty + ask_qty)
microprice = mid + alpha * (I - 0.5) * spread
```

See `docs/model_description.md` for more detail.

## Test

The upstream scaffold includes Catch2 tests. To build tests, allow CMake to fetch
the test dependency and configure with `BUILD_TESTS=ON`:

```bash
cmake -B build -S . -DBUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure -j
```
