#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "pandas",
#   "matplotlib",
# ]
# ///

import argparse
import sys

import matplotlib.dates as mdates
import matplotlib.gridspec as gridspec
import matplotlib.pyplot as plt
import pandas as pd


def load(path: str) -> pd.DataFrame:
    df = pd.read_csv(path)
    df["ts"] = pd.to_datetime(df["timestamp_ns"], unit="ns")
    df["drawdown"] = df["equity"] - df["equity"].cummax()
    df["fill_rate"] = df["fill_count"].diff().fillna(0)
    return df


def summary(df: pd.DataFrame) -> None:
    t0, t1 = df["ts"].iloc[0], df["ts"].iloc[-1]
    duration_h = (t1 - t0).total_seconds() / 3600

    total_pnl = df["equity"].iloc[-1] - df["equity"].iloc[0]
    max_dd = df["drawdown"].min()
    max_eq = df["equity"].max()

    eq_chg = df["equity"].diff().dropna()
    sharpe = (
        eq_chg.mean() / eq_chg.std() * (len(eq_chg) ** 0.5)
        if eq_chg.std() > 0
        else float("nan")
    )

    print("\n--- PnL Summary ---")
    print(
        f"Period:        {t0:%Y-%m-%d %H:%M}  ->  {t1:%Y-%m-%d %H:%M}  ({duration_h:.1f}h)"
    )
    print(f"Total PnL:     {total_pnl:+.2f}")
    print(f"Max equity:    {max_eq:.2f}")
    print(f"Max drawdown:  {max_dd:.2f}")
    print(f"Fill count:    {int(df['fill_count'].iloc[-1]):,}")
    print(f"Volume:        {df['volume'].iloc[-1]:.4g}")
    print(f"Sharpe (raw):  {sharpe:.2f}")


def plot(df: pd.DataFrame, output_path: str | None) -> None:
    fig = plt.figure(figsize=(14, 11))
    fig.suptitle("Market Maker - PnL Dynamics", fontsize=13, fontweight="bold", y=0.98)

    gs = gridspec.GridSpec(
        4,
        1,
        figure=fig,
        hspace=0.08,
        height_ratios=[3, 1.5, 2, 2],
    )
    axes = [fig.add_subplot(gs[i]) for i in range(4)]
    for ax in axes[:-1]:
        ax.sharex(axes[0])
        plt.setp(ax.get_xticklabels(), visible=False)

    ts = df["ts"]

    # panel 0: equity (total PnL) + cash (realized PnL)
    ax = axes[0]
    ax.plot(ts, df["equity"], color="#2171b5", linewidth=0.9, label="Equity (MtM)")
    ax.plot(
        ts,
        df["cash"],
        color="#e6550d",
        linewidth=0.9,
        linestyle="--",
        label="Cash (realized)",
    )
    ax.axhline(0, color="#999", linewidth=0.5, linestyle="--")
    ax.fill_between(
        ts, df["equity"], 0, where=df["equity"] >= 0, alpha=0.12, color="#2171b5"
    )
    ax.fill_between(
        ts, df["equity"], 0, where=df["equity"] < 0, alpha=0.15, color="#d73027"
    )
    ax.set_ylabel("PnL", fontsize=9)
    ax.legend(loc="upper left", fontsize=8)
    ax.grid(True, alpha=0.25)

    # panel 1: drawdown
    ax = axes[1]
    ax.fill_between(ts, df["drawdown"], 0, color="#d73027", alpha=0.6, label="Drawdown")
    ax.set_ylabel("Drawdown", fontsize=9)
    ax.legend(loc="lower left", fontsize=8)
    ax.grid(True, alpha=0.25)

    # panel 2: position
    ax = axes[2]
    ax.plot(ts, df["position"], color="#f16913", linewidth=0.8, label="Position")
    ax.axhline(0, color="#999", linewidth=0.5, linestyle="--")
    ax.fill_between(ts, df["position"], 0, alpha=0.15, color="#f16913")
    ax.set_ylabel("Position", fontsize=9)
    ax.legend(loc="upper left", fontsize=8)
    ax.grid(True, alpha=0.25)

    # panel 3: mid-price
    ax = axes[3]
    ax.plot(ts, df["mid_price"], color="#41ab5d", linewidth=0.7, label="Mid Price")
    ax.set_ylabel("Mid Price", fontsize=9)
    ax.set_xlabel("Time (UTC)", fontsize=9)
    ax.legend(loc="upper left", fontsize=8)
    ax.grid(True, alpha=0.25)

    axes[-1].xaxis.set_major_formatter(mdates.DateFormatter("%m-%d\n%H:%M"))
    axes[-1].xaxis.set_major_locator(mdates.AutoDateLocator())
    plt.setp(axes[-1].get_xticklabels(), fontsize=7)

    plt.tight_layout(rect=[0, 0, 1, 0.97])

    if output_path:
        plt.savefig(output_path, dpi=150, bbox_inches="tight")
        print(f"Plot saved -> {output_path}")
    else:
        plt.show()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Plot PnL dynamics from backtester results."
    )
    parser.add_argument(
        "input",
        nargs="?",
        default="results/pnl_timeseries.csv",
        help="Path to pnl_timeseries.csv (default: results/pnl_timeseries.csv)",
    )
    parser.add_argument(
        "--output",
        "-o",
        default=None,
        help="Save plot to file instead of displaying (e.g. results/pnl.png)",
    )
    args = parser.parse_args()

    print(f"Loading {args.input} ...")
    try:
        df = load(args.input)
    except FileNotFoundError:
        print(f"Error: file not found: {args.input}", file=sys.stderr)
        print(
            "Run the backtester first:  ./build/bin/hft-market-making", file=sys.stderr
        )
        sys.exit(1)

    print(f"Loaded {len(df):,} rows")
    summary(df)
    plot(df, args.output)


if __name__ == "__main__":
    main()
