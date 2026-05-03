#!/usr/bin/env python3
import sys
import os

try:
    import polars as pl
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError as e:
    print(f"Missing dependency: {e}\npip install polars matplotlib")
    sys.exit(1)

RESULTS = "results"
VARIANTS = ["mid", "microprice"]
COLORS = {"mid": "#1f77b4", "microprice": "#ff7f0e"}


def load_equity(variant: str) -> pl.DataFrame:
    path = os.path.join(RESULTS, variant, "equity.csv")
    df = pl.read_csv(path)
    df = df.with_columns(
        ((pl.col("ts") - pl.col("ts").first()) / 1e9 / 3600).alias("hours")
    )
    n = len(df)
    step = max(1, n // 5000)
    return df[::step]


def load_summary() -> pl.DataFrame:
    rows = []
    for v in VARIANTS:
        path = os.path.join(RESULTS, v, "summary.txt")
        if not os.path.exists(path):
            continue
        d: dict = {"variant": v}
        with open(path) as f:
            in_metrics = False
            for line in f:
                line = line.strip()
                if line == "metric,value":
                    in_metrics = True
                    continue
                if in_metrics and "," in line:
                    key, val = line.split(",", 1)
                    try:
                        d[key] = float(val)
                    except ValueError:
                        d[key] = val
        rows.append(d)
    if not rows:
        return pl.DataFrame()
    keys = list({k for r in rows for k in r})
    return pl.DataFrame({k: [r.get(k) for r in rows] for k in keys})


def equity_plot(dfs: dict) -> None:
    fig, axes = plt.subplots(2, 1, figsize=(12, 8), sharex=True)

    for v, df in dfs.items():
        axes[0].plot(df["hours"], df["equity"], lw=0.6, color=COLORS[v], label=v)
        axes[1].plot(df["hours"], df["inventory"], lw=0.4, color=COLORS[v], label=v, alpha=0.8)

    axes[0].set_ylabel("Equity (mark-to-market)")
    axes[0].axhline(0, color="black", lw=0.5, ls="--")
    axes[0].legend()
    axes[0].grid(True, alpha=0.3)

    axes[1].set_ylabel("Net inventory (units)")
    axes[1].set_xlabel("Elapsed time (hours)")
    axes[1].axhline(0, color="black", lw=0.5, ls="--")
    axes[1].legend()
    axes[1].grid(True, alpha=0.3)

    fig.suptitle("Avellaneda-Stoikov Market Making - 6-Day Backtest")
    fig.tight_layout()
    out = os.path.join(RESULTS, "equity_compare.png")
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"Saved {out}")


def microstructure_plot(dfs: dict) -> None:
    fig, axes = plt.subplots(2, 1, figsize=(12, 6), sharex=True)

    for v, df in dfs.items():
        sub = df.filter(pl.col("hours") <= 6)
        axes[0].plot(sub["hours"], sub["equity"], lw=0.8, color=COLORS[v], label=v)
        axes[1].plot(sub["hours"], sub["inventory"], lw=0.6, color=COLORS[v], label=v, alpha=0.9)

    axes[0].set_ylabel("Equity")
    axes[0].axhline(0, color="black", lw=0.5, ls="--")
    axes[0].legend()
    axes[0].grid(True, alpha=0.3)

    axes[1].set_ylabel("Net inventory")
    axes[1].set_xlabel("Elapsed time (hours)")
    axes[1].axhline(0, color="black", lw=0.5, ls="--")
    axes[1].legend()
    axes[1].grid(True, alpha=0.3)

    fig.suptitle("First 6 Hours - Equity and Inventory Detail")
    fig.tight_layout()
    out = os.path.join(RESULTS, "microstructure_detail.png")
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"Saved {out}")


def summary_table(summary: pl.DataFrame) -> None:
    if summary.is_empty():
        print("No summary data found (run experiments first)")
        return
    out = os.path.join(RESULTS, "summary_table.csv")
    summary.write_csv(out)
    print(f"Saved {out}")
    print(summary)


def main() -> None:
    os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

    dfs = {}
    for v in VARIANTS:
        path = os.path.join(RESULTS, v, "equity.csv")
        if os.path.exists(path):
            dfs[v] = load_equity(v)
        else:
            print(f"Missing {path}, skipping {v}")

    if dfs:
        equity_plot(dfs)
        microstructure_plot(dfs)

    summary = load_summary()
    summary_table(summary)


if __name__ == "__main__":
    main()
