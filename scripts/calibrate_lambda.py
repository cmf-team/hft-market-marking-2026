#!/usr/bin/env python3
"""Calibrate Avellaneda-Stoikov fill intensity lambda(delta) = A * exp(-k * delta)
on a tardis-style trade tape and (optionally) update an ini-config in place.

The script is deliberately self-contained -- one file, one external dependency
(pandas) and works on the full ~2 GB dataset thanks to streaming reads.

Examples
--------
# Print A, k for the full dataset and write a CSV for sanity-plotting:
python3 scripts/calibrate_lambda.py \
    --lob    ../../MD/lob.csv \
    --trades ../../MD/trades.csv \
    --out    reports/lambda_fit.csv

# Same, but also patch ini-configs (only the `k = ...` line is touched):
python3 scripts/calibrate_lambda.py \
    --lob    ../../MD/lob.csv \
    --trades ../../MD/trades.csv \
    --update config/as2008_full.cfg config/as2018_full.cfg
"""
from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path
from typing import Iterable, Tuple

import numpy as np
import pandas as pd


# ---------------------------------------------------------------------------
# Streaming readers: handle ~1 GB files without OOM-ing.
# ---------------------------------------------------------------------------
def stream_mid(lob_path: Path, chunksize: int = 200_000) -> pd.DataFrame:
    """Read lob.csv in chunks, keep only timestamp and mid (best bid+ask)/2."""
    cols = ["local_timestamp", "asks[0].price", "bids[0].price"]
    out = []
    for chunk in pd.read_csv(lob_path, usecols=cols, chunksize=chunksize):
        mid = 0.5 * (chunk["asks[0].price"] + chunk["bids[0].price"])
        out.append(pd.DataFrame({"ts_us": chunk["local_timestamp"], "mid": mid}))
    df = pd.concat(out, ignore_index=True)
    df.sort_values("ts_us", inplace=True, kind="mergesort")
    df.reset_index(drop=True, inplace=True)
    return df


def stream_trades(trades_path: Path, chunksize: int = 500_000) -> pd.DataFrame:
    """Read trades.csv in chunks. Tardis layout: idx, ts_us, side, price, amount."""
    out = []
    for chunk in pd.read_csv(trades_path, chunksize=chunksize):
        # Defensive renaming -- column names vary across exports.
        chunk.columns = [c.strip().lower() for c in chunk.columns]
        if "local_timestamp" in chunk.columns:
            chunk.rename(columns={"local_timestamp": "ts_us"}, inplace=True)
        out.append(chunk[["ts_us", "side", "price", "amount"]])
    df = pd.concat(out, ignore_index=True)
    df.sort_values("ts_us", inplace=True, kind="mergesort")
    df.reset_index(drop=True, inplace=True)
    return df


# ---------------------------------------------------------------------------
# Calibration core.
# ---------------------------------------------------------------------------
PRICE_SCALE = 10_000  # тот же масштаб, что и Price в C++ (см. backtest_data_reader.h)


def merge_trades_with_mid(trades: pd.DataFrame, mids: pd.DataFrame) -> pd.DataFrame:
    """Attach mid-at-trade-time via merge_asof (backward) and compute delta.

    `delta` сразу выражается в "центах" (price * 10_000) -- ровно те же единицы,
    что C++ использует внутри для Price/sigma. Это критично: иначе k_hat
    вылезет в неправильной размерности (1/dollars вместо 1/cents).
    """
    merged = pd.merge_asof(trades, mids, on="ts_us", direction="backward")
    merged.dropna(subset=["mid"], inplace=True)
    merged["delta"] = np.where(
        merged["side"].astype(str).str.lower().eq("sell"),
        merged["price"] - merged["mid"],   # sell-aggressor crosses bid below mid
        merged["mid"] - merged["price"],   # buy-aggressor crosses ask above mid
    ) * PRICE_SCALE
    merged = merged[merged["delta"] > 0].copy()
    return merged


def fit_exponential(
    deltas: np.ndarray, horizon_seconds: float, n_grid: int = 40
) -> Tuple[float, float, pd.DataFrame]:
    """Empirical lambda(x) = #{trades with delta>=x} / horizon, then OLS in log."""
    if len(deltas) < 50 or horizon_seconds <= 0:
        raise ValueError("Not enough trades or zero horizon for calibration.")
    lo = np.quantile(deltas, 0.05)
    hi = np.quantile(deltas, 0.95)
    if not (math.isfinite(lo) and math.isfinite(hi)) or hi <= lo:
        raise ValueError("Degenerate delta distribution.")
    grid = np.linspace(lo, hi, n_grid)
    sorted_d = np.sort(deltas)
    counts = sorted_d.size - np.searchsorted(sorted_d, grid, side="left")
    lam = counts / horizon_seconds
    mask = lam > 0
    slope, intercept = np.polyfit(grid[mask], np.log(lam[mask]), 1)
    k_hat = -slope
    A_hat = math.exp(intercept)
    fit = pd.DataFrame(
        {"delta": grid, "lambda_empirical": lam, "lambda_fit": A_hat * np.exp(-k_hat * grid)}
    )
    return A_hat, k_hat, fit


# ---------------------------------------------------------------------------
# Ini-config patching.
# ---------------------------------------------------------------------------
def patch_ini(path: Path, key: str, value: str) -> bool:
    """Replace `key = ...` (preserving comments). Returns True if modified."""
    text = path.read_text().splitlines()
    changed = False
    for i, line in enumerate(text):
        stripped = line.strip()
        if not stripped or stripped[0] in (";", "#", "["):
            continue
        if "=" not in stripped:
            continue
        lhs = stripped.split("=", 1)[0].strip()
        if lhs == key:
            indent = line[: len(line) - len(line.lstrip())]
            text[i] = f"{indent}{key:<20} = {value}    ; updated by calibrate_lambda.py"
            changed = True
            break
    if changed:
        path.write_text("\n".join(text) + "\n")
    return changed


# ---------------------------------------------------------------------------
# CLI.
# ---------------------------------------------------------------------------
def main(argv: Iterable[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--lob", required=True, type=Path)
    p.add_argument("--trades", required=True, type=Path)
    p.add_argument("--out", type=Path, default=Path("reports/lambda_fit.csv"))
    p.add_argument(
        "--update",
        nargs="*",
        default=[],
        type=Path,
        help="cfg/ini files to patch with the calibrated `k = ...` value.",
    )
    args = p.parse_args(argv)

    print(f"[calibrate] reading lob: {args.lob}", file=sys.stderr)
    mids = stream_mid(args.lob)
    print(f"[calibrate] read {len(mids):,} mid samples", file=sys.stderr)

    print(f"[calibrate] reading trades: {args.trades}", file=sys.stderr)
    trades = stream_trades(args.trades)
    print(f"[calibrate] read {len(trades):,} trades", file=sys.stderr)

    merged = merge_trades_with_mid(trades, mids)
    print(f"[calibrate] valid trades for fit: {len(merged):,}", file=sys.stderr)
    horizon_us = float(merged["ts_us"].max() - merged["ts_us"].min())
    horizon_s = horizon_us / 1e6
    A, k, fit = fit_exponential(merged["delta"].to_numpy(), horizon_s)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    fit.to_csv(args.out, index=False)

    print(f"\n=== Calibration result ===")
    print(f"horizon_seconds : {horizon_s:,.1f}")
    print(f"trades_used     : {len(merged):,}")
    print(f"A (intercept)   : {A:.6g}")
    print(f"k (decay)       : {k:.6g}")
    print(f"fit table       : {args.out}")

    for ini in args.update:
        if patch_ini(ini, "k", f"{k:.6g}"):
            print(f"[calibrate] patched {ini}: k = {k:.6g}")
        else:
            print(f"[calibrate] WARNING: no `k = ...` line in {ini}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
