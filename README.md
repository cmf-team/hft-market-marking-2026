# Backtester

A high-frequency trading backtester written in C++20. It replays historical L2 order book snapshots and trade prints through a simulated exchange with realistic microstructure modeling.

## Key Features

- **Latency emulation** -- configurable independent delays (in microseconds) for order submission, cancellation, and fill notification. Orders are evaluated at *delivery time*, not submission time, so market moves during transit cause realistic rejections.
- **Queue position modeling** -- pessimistic queue model estimates how much volume sits ahead of a newly posted order and erodes that position as public trades execute. Orders only fill after their queue clears. Cancellations are attributed to orders *behind* yours, never optimistically advancing your position.
- **Post-only matching** -- now accepts only maker limit orders. A post-only check at delivery time rejects orders that would cross the book.
- **Merged event stream** -- lazily merges LOB snapshots and trade prints into a single chronological stream (snapshots before trades on ties), with zero extra allocations.
- **Portfolio & PnL** -- weighted-average cost accounting with separate realized/unrealized PnL, all in integer tick arithmetic (no floating-point drift).
- **Reporting** -- `report.txt` summary (fills, volume, PnL, max drawdown) and `equity.csv` time-series with configurable sample interval.

## Building

### With Docker (recommended)

Requires Docker and Docker Compose.

```bash
# Build the Docker image (ubuntu:24.04 + clang, cmake, ninja, gtest)
make image

# Compile the project inside the container
make build

# Run unit tests
make test
```

### With a local toolchain

Requires a C++20 compiler, CMake >= 3.22, Ninja (optional), and libgtest-dev.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Quick Start

Place your data from expample in the `data/` folder:

```
data/
  lob.csv       # L2 order book snapshots
  trades.csv    # trade prints
  config.txt    # already provided with defaults for example
```

Then run:

```bash
make run-with-config
```

This builds the project (if needed) and runs the backtester using `data/config.txt`. Results are written to the `out/` directory (`report.txt` and `equity.csv`).

You can override any config parameter from the command line:

```bash
make run-with-config ARGS="--quote-size 5 --submit-us 250"
```

## Configuration

The backtester accepts a config file (`--config <path>`) and/or CLI flags. CLI flags override config file values.

### Config file format

Key-value pairs, one per line. `#` starts a comment.

```conf
# ----- Input / output paths -----
lob    = data/lob.csv
trades = data/trades.csv
out    = out

# ----- Instrument spec -----
tick_size = 1e-7      # smallest price increment in human units
qty_scale = 1         # multiplier: CSV amount -> internal integer Qty

# ----- Latency model (microseconds) -----
submit_us = 100       # strategy -> exchange delay for new orders
cancel_us = 100       # strategy -> exchange delay for cancels
fill_us   = 100       # exchange -> strategy delay for acks/fills/rejects

# ----- Reporting -----
sample_us = 1000000   # equity-curve sample interval (1s). 0 = every book event

# ----- Strategy (StaticQuoter) -----
quote_size = 1        # passive quote size on each side
```

### CLI flags

```
backtester --lob <lob.csv> --trades <trades.csv> --out <out_dir>
           [--config <config_path>]
           [--tick-size <double>] [--qty-scale <double>]
           [--submit-us <int>] [--cancel-us <int>] [--fill-us <int>]
           [--sample-us <int>] [--quote-size <int>]
```

### Parameters reference

| Parameter | CLI flag | Default | Description |
|-----------|----------|---------|-------------|
| `lob` | `--lob` | *(required)* | Path to L2 snapshot CSV |
| `trades` | `--trades` | *(required)* | Path to trades CSV |
| `out` | `--out` | *(required)* | Output directory |
| `tick_size` | `--tick-size` | `1e-7` | Smallest price increment (human units) |
| `qty_scale` | `--qty-scale` | `1.0` | CSV amount to internal Qty multiplier |
| `submit_us` | `--submit-us` | `0` | Order submission latency (us) |
| `cancel_us` | `--cancel-us` | `0` | Cancel latency (us) |
| `fill_us` | `--fill-us` | `0` | Fill notification latency (us) |
| `sample_us` | `--sample-us` | `1000000` | Equity curve sample interval (us) |
| `quote_size` | `--quote-size` | `1` | Quote size for StaticQuoter |

### Running directly (without config file)

```bash
# Docker
make run ARGS="--lob data/lob.csv --trades data/trades.csv --out out --submit-us 200 --fill-us 200 --quote-size 10"

# Local
./build/src/backtester --lob data/lob.csv --trades data/trades.csv --out out --submit-us 200 --fill-us 200 --quote-size 10
```

## Data Format

### LOB snapshots (`lob.csv`)

25-level L2 order book snapshots. Header row followed by data rows:

```
,local_timestamp,asks[0].price,asks[0].amount,bids[0].price,bids[0].amount,asks[1].price,asks[1].amount,bids[1].price,bids[1].amount,...,asks[24].price,asks[24].amount,bids[24].price,bids[24].amount
0,1700000000000000,0.0110500,150,0.0110400,200,0.0110600,300,0.0110300,250,...
```

- First column is a row index (ignored)
- `local_timestamp` -- microseconds (int64)
- 25 ask levels + 25 bid levels, each with price (double) and amount (double)
- Prices must be on the tick grid (`tick_size`); off-grid prices cause an error

### Trades (`trades.csv`)

```
,local_timestamp,side,price,amount
0,1700000000000100,sell,0.0110450,50
1,1700000000000200,buy,0.0110460,30
```

- First column is a row index (ignored)
- `side` -- `buy` or `sell`
- `price` -- double, must be on tick grid
- `amount` -- double, converted to integer Qty via `qty_scale`

## Output

Results are written to the `--out` directory:

- **`report.txt`** -- human-readable summary: submitted/rejected/filled counts, total volume, gross PnL, max drawdown, instrument spec, latency settings
- **`equity.csv`** -- time-series with columns `ts_us,equity` sampled at `sample_us` intervals

## Make Targets

| Target | Description |
|--------|-------------|
| `make image` | Build the Docker image |
| `make up` | Start the dev container |
| `make down` | Stop the dev container |
| `make build` | Compile the project |
| `make test` | Run unit tests |
| `make run ARGS="..."` | Run with explicit CLI arguments |
| `make run-with-config` | Run using `data/config.txt` |
| `make shell` | Open a bash shell in the container |
| `make clean` | Remove build artifacts |
