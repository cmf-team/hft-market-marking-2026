# Strategy Model Description

## Avellaneda-Stoikov

The classic strategy implements the finite-horizon approximation from Avellaneda and
Stoikov (2008). On every quote refresh it estimates a fair price `s`, shifts it by
inventory, and places one bid plus one ask:

```text
r = s - q * gamma * sigma^2 * tau
spread = gamma * sigma^2 * tau + (2 / gamma) * log(1 + gamma / k)
bid = r - spread / 2
ask = r + spread / 2
```

Where:

- `q` is current inventory.
- `gamma` is risk aversion.
- `sigma` is absolute price volatility per square-root second.
- `tau` is the risk horizon in seconds.
- `k` controls the decay of fill intensity as quotes move away from mid.

The implementation uses the assignment execution rule: a limit order fills when a
market price crosses its order level. Newly generated quotes are clamped to the
visible best bid and best ask to avoid accidentally becoming taker orders.

## Microprice Extension

The enhanced strategy replaces the mid-price with a level-1 microprice estimate
inspired by Stoikov (2018). With best bid `Pb`, best ask `Pa`, bid size `Qb`, and
ask size `Qa`:

```text
I = Qb / (Qb + Qa)
microprice = mid + alpha * (I - 0.5) * spread
```

With `alpha = 1`, this equals the common weighted mid-price. This is a practical
online approximation of the paper's finite-state estimator: it uses the observed
imbalance as a short-horizon fair-price adjustment without an offline transition
matrix calibration step.

## Volatility

If `as_sigma` is positive, the strategy uses that configured value. Otherwise it
updates an EWMA estimate of absolute price variance per second from top-of-book
mid-price changes and applies `as_sigma_floor` while the estimate warms up.

## Risk Controls

The strategy respects `max_position` on both sides. Near the limit it reduces the
order size instead of submitting a quote that would breach the inventory cap.
