# CMF HFT Market-Making Backtester

## Directory structure

```
.
├── 3rdparty                    # place holder for 3rd party libraries (downloaded during the build)
├── build                       # local build tree used by CMake
├    ├── bin                    # generated binaries
├    ├── lib                    # generated libs (including those, which are built from 3rd party sources)
├    ├── cfg                    # generated config files (if any)
├    └── include                # generated include files (installed during the build for 3rd party sources)
├── cmake                       # cmake helper scripts
├── config                      # example config files
├── scripts                     # shell (and other) maintenance scripts
├── src                         # source files
├    ├── common                 # common utility files
├    ├── ...                    # ...
├    └── main                   # main() for hft-market-making app
├── test                        # unit-tests and other tests
├── CMakeLists.txt              # main build script
└── README.md                   # this README
```

## OS

Our primary platform is Linux, but nothing prevents it to be built and run on other OS.
The following commands are for Linux users.
Other users are encouraged to add the corresponding instructions for required steps in this README.

## Build

Install dependencies once:

```
sudo apt install -y cmake g++ clang-format
```

Build using cmake:

```
cmake -B build -S .
cmake --build build -j
```

or

```
mkdir -p build
pushd build
cmake ..
make -j VERBOSE=1
popd
```

## Test

To run unit tests:

```
ctest --test-dir build -j
```

or

```
pushd build
ctest -j
popd
```

or

```
build/bin/test/hft-market-making-tests
```

## Data

The backtester expects a directory containing two CSV files:

```
<data_dir>/
├── trades.csv   # individual trade executions
└── lob.csv      # limit order book snapshots (25 levels)
```

**trades.csv** columns:

| Column | Type | Description |
| --- | --- | --- |
| *(index)* | int | row index (ignored) |
| `local_timestamp` | uint64 | nanosecond/microsecond Unix timestamp |
| `side` | string | `buy` or `sell` |
| `price` | float | execution price |
| `amount` | float | trade quantity |

**lob.csv** columns:

| Column | Type | Description |
| --- | --- | --- |
| *(index)* | int | row index (ignored) |
| `local_timestamp` | uint64 | nanosecond/microsecond Unix timestamp |
| `asks[i].price`, `asks[i].amount`, `bids[i].price`, `bids[i].amount` | float | price and size for levels 0–24 (interleaved per level) |

Prices are stored as raw floats and scaled internally to `uint64_t × 1e9`.

## Strategies

### MicroPrice

Econometric market-making using order book imbalance and spread state.

Calibration phase (first N LOB snapshots) learns a Markov transition model
over `(imbalance bucket, spread level)` states. After calibration the model
produces an optimal mid-price adjustment and symmetric quotes around it.

Key parameters (configurable in `main.cpp`):

| Parameter | Default | Description |
| --- | --- | --- |
| `calib_snapshots` | 5000 | warm-up LOB snapshots before quoting |
| `n_imbal_buckets` | 10 | imbalance discretisation bins |
| `n_spread_levels` | 5 | spread levels in ticks |
| `order_qty` | 1.0 | order size |
| `max_inventory` | 10.0 | halt quoting above this inventory |

### Avellaneda-Stoikov

Classic inventory-aware market-making model.

Computes a reservation price penalised by current inventory and a
closed-form optimal spread based on volatility, risk aversion, and order
arrival intensity.

Key parameters:

| Parameter | Default | Description |
| --- | --- | --- |
| `gamma` | 0.1 | risk aversion coefficient γ |
| `k` | 1.5 | order arrival intensity |
| `session_secs` | 600.0 | trading horizon T (seconds) |
| `vol_window` | 100 | rolling window for σ estimation |
| `max_inventory` | 10.0 | halt quoting above this inventory |

## Run

```bash
build/bin/hft-market-making <data_dir> <strategy>
```

`strategy` is one of `microprice` or `avellaneda-stoikov`.

Example:

```bash
build/bin/hft-market-making /path/to/data/MD microprice
build/bin/hft-market-making /path/to/data/MD avellaneda-stoikov
```

Output:

```
Loaded events: <N>  first_ts=<timestamp>
PnL:    <value>
Sharpe: <value>
Fills:  <count>
Load trades:     <ms> ms
Load orderbooks: <ms> ms
Sort + merge:    <ms> ms
Engine run:      <ms> ms
```

## Contributing

Install UV, create a virtual environment, and install the project dependencies:

```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
uv sync
```

Then activate the virtual environment and set up the git pre-commit hooks:

```bash
source .venv/bin/activate
pre-commit install
```

After that, formatting and linting will run automatically before each commit.
If the source code does not meet the required formatting rules, the hook will
modify the files and stop the commit, and you will need to stage the updated
changes manually.

To run formatting and linting yourself, use one of these commands:

```bash
pre-commit run --files file.py
pre-commit run --all-files
```

The current pre-commit hooks do the following:
- format and lint C++ code with `clang-format`;
- format and lint Python code with `ruff`;
- strip outputs from Jupyter notebooks.
