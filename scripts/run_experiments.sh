#!/usr/bin/env bash
set -e
REPO="$(dirname "$0")/.."
BIN="$REPO/build/bin/hft-market-making"

echo "=== Building ==="
cmake -S "$REPO" -B "$REPO/build" -DCMAKE_BUILD_TYPE=Release -DDISABLE_PRINT=1
cmake --build "$REPO/build" -j

cd "$REPO"

for mode in mid microprice; do
    echo ""
    echo "=== Running A-S ($mode) ==="
    outdir="results/$mode"
    mkdir -p "$outdir"
    "$BIN" "config/as_${mode}.conf" 2>&1 | tee "$outdir/summary.txt"
    for f in executions.csv equity.csv stoikov_mm.log; do
        [ -f "$f" ] && mv "$f" "$outdir/"
    done
done

echo ""
echo "=== Generating plots ==="
python3 scripts/plot_results.py

echo ""
echo "Done. Results in results/"
