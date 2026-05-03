# Performance Results

## Data

- Full simulation data: https://drive.google.com/file/d/1DiP5arvCEMxLHZ0R2mAS4lcMjnPHSrEJ
- Local full files: `lob.csv`, `trades.csv`
- Sample files: `data/sample_lob.csv`, `data/sample_trades.csv`
- Execution rule: market price crosses order level.

## Full Data Experiments

All full-data runs processed `22,901,679` merged events with `1,036,690` LOB rows and `21,864,989` trade rows.

| Strategy | Config | PnL | Max drawdown | Ending inventory | Peak inventory | Turnover | Fill rate |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Simple baseline | `configs/cmf_config.ini` | `-3.34664754` | `5.29345000` | `1000` | `1000` | `610.22680246` | `0.00008925` |
| Avellaneda-Stoikov | `configs/avellaneda_stoikov_cmf_config.ini` | `-819.06950000` | `819.10145000` | `0` | `98000` | `8436659.18670009` | `0.53705685` |
| Microprice-AS | `configs/microprice_as_cmf_config.ini` | `-683.66805000` | `716.34515000` | `-95000` | `100000` | `8630253.23060014` | `0.49372756` |

## Sample Experiments

The sample configs use 5,000 LOB rows and 20,000 trade rows for fast smoke tests.

| Strategy | Config | PnL | Max drawdown | Ending inventory | Turnover | Fill rate |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Avellaneda-Stoikov | `configs/avellaneda_stoikov_sample_config.ini` | `-5.57555000` | `6.75555000` | `-59000` | `35601.79510000` | `0.39386158` |
| Microprice-AS | `configs/microprice_as_sample_config.ini` | `-3.91715000` | `8.75230000` | `-79000` | `36132.90670000` | `0.33133996` |

## Interpretation

The microprice-enhanced variant improved full-data PnL by `135.40145` and reduced max drawdown by `102.75630` versus classic AS under the default parameters.
Both AS variants trade far more actively than the baseline because they continuously quote both sides.
The negative PnL and high inventory usage show that the current crossing-only emulator and uncalibrated `gamma`, `k`, refresh, and microprice parameters need tuning before the strategy can be considered robust.

## Next Steps

- Calibrate `k` from observed fill probability by distance from mid.
- Tune `gamma`, refresh interval, and `max_position` with parameter sweeps.
- Replace the weighted-mid microprice proxy with the finite-state transition estimator described in Stoikov (2018).
- Add queue position, fees, latency, and partial fills for more realistic execution.
