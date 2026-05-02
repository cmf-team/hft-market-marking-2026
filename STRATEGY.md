# Strategy: Avellaneda–Stoikov Market Making

## Model Description

### Avellaneda–Stoikov (2008)

A market maker continuously quotes a bid and ask around a **reservation price** that accounts for inventory risk.

**Reservation price** — where we'd trade for free given current inventory:
```
r = s - q · γ · σ² · T
```
- `s` — reference price (mid or microprice)
- `q` — current inventory
- `γ` — risk aversion (higher → more aggressive inventory mean-reversion)
- `σ` — mid-price volatility per tick
- `T` — time horizon

**Optimal spread** — wider when volatile or near deadline, tighter when order flow is intense:
```
δ* = γ · σ² · T + (2/γ) · ln(1 + γ/k)
```
- `k` — order arrival intensity (higher k → tighter spread)

**Quotes:**
```
bid = r - δ*/2
ask = r + δ*/2
```

The key idea: when inventory `q > 0` (long), `r < s`, so we quote lower to sell faster. When `q < 0` (short), `r > s`, so we quote higher to buy faster.

### Microprice Extension (Stoikov 2018)

Plain mid-price `(ask + bid) / 2` ignores order book imbalance. Microprice weights by opposite-side depth:

```
microprice = (ask · V_bid + bid · V_ask) / (V_bid + V_ask)
```

If `V_bid > V_ask` (more buyers), microprice sits closer to the ask — price is likely to move up. This gives a better estimate of fair value than mid.

Toggle via `use_microprice` in `config/strategy.cfg`.

---

## Technical Architecture

```
DataReader (two-pointer merge of LOB + trades by timestamp)
    │
    ├─ LOB tick ──► StoikovMM::reactToLob()
    │                   computes r, δ*, places bid/ask orders
    │                   ──► BacktestEngine::applyOrders()
    │
    └─ Trade tick ──► BacktestEngine::reactToMarketTrade()
                          checks each open order for price crossing
                          returns fills (with partial fill support)
                          ──► StoikovMM::reactToExecution()
                                  updates inventory
```

**Execution assumption:** fill occurs when market trade price crosses the limit order level (strict: sell trade fills buy orders, buy trade fills sell orders).

**Output files:**
- `results.csv` — every fill with `timestamp, side, price, amount, position, cash_flow, mtm_pnl`
- `report.txt` — summary: trades, final PnL, position, turnover, max drawdown

**Config:** `config/strategy.cfg` → passed as first CLI argument, falls back to defaults.

---

## Performance Results

Run the backtest and inspect:
- `data/results.csv` — per-trade breakdown
- `data/report.txt` — summary metrics

Key columns in `results.csv`:
| column | meaning |
|---|---|
| `cash_flow` | cumulative cash: Σ(sell·price) − Σ(buy·price) |
| `mtm_pnl` | `cash_flow + position · fill_price` (mark-to-market at fill) |
| `position` | signed inventory after each fill |

In pandas:
```python
import pandas as pd
df = pd.read_csv("data/results.csv")
df["timestamp"] = pd.to_datetime(df["timestamp"], unit="us")
df.set_index("timestamp")[["mtm_pnl", "position"]].plot(subplots=True)
```

---

## Improvement Roadmap

1. **Calibrate σ dynamically** — compute rolling mid-price std dev from LOB stream instead of static param
2. **Inventory limits** — hard cap on `|q|`, stop quoting on the side that would increase exposure
3. **Multi-level quoting** — quote at multiple depths with decreasing size, not just best bid/ask
4. **Adverse selection filter** — reduce size or widen spread when order book imbalance exceeds threshold
5. **Fee model** — subtract maker/taker fees from PnL to get realistic net results
6. **Sharpe ratio** — compute rolling PnL time series (needs mid-price at each LOB tick, not just at fills)
