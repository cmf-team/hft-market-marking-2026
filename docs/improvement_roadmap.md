# Improvement Roadmap

1. Add queue-position and partial-fill modeling so maker fills depend on visible
   depth and order priority instead of only price crossing.
2. Calibrate Avellaneda-Stoikov `k` from empirical fill probability by quote
   distance, rather than using a static config value.
3. Replace the weighted-mid microprice approximation with the full Stoikov (2018)
   finite-state estimator over imbalance and spread transition matrices.
4. Add latency, fees, tick-size validation, and adverse-selection metrics.
5. Run parameter sweeps over `gamma`, `k`, quote refresh, and microprice alpha,
   then compare PnL, turnover, drawdown, fill rate, and inventory stability.
