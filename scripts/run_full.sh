#!/usr/bin/env bash
# scripts/run_full.sh -- end-to-end full-dataset run:
#   1) build (если ещё не собрано)
#   2) калибровка lambda(delta) на полных данных, патч cfg-файлов
#   3) три прогона (naive, as2008, as2018) на полных данных
#   4) сводный отчёт в reports/full_run_compare.csv
#
# Запускать из корня репозитория. По умолчанию ожидает данные в MD/{lob,trades}.csv.

set -euo pipefail

cd "$(dirname "$0")/.."

LOB="${LOB:-MD/lob.csv}"
TRADES="${TRADES:-MD/trades.csv}"
PYTHON="${PYTHON:-python3}"
BIN="build/bin/hft-market-making"

echo ">>> Build"
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF >/dev/null
cmake --build build -j

mkdir -p reports

echo ">>> Calibrate lambda(delta) on the full dataset"
"${PYTHON}" scripts/calibrate_lambda.py \
    --lob "${LOB}" --trades "${TRADES}" \
    --out reports/lambda_fit.csv \
    --update config/as2008_full.cfg config/as2018_full.cfg

run_one() {
    local cfg="$1" tag="$2"
    echo
    echo ">>> Run ${tag}"
    /usr/bin/time -h "${BIN}" "${cfg}" \
        > "reports/${tag}_full_stdout.txt" 2> "reports/${tag}_full_stderr.txt" \
        || { echo "  FAILED, see reports/${tag}_full_stderr.txt"; return 1; }
    tail -n 25 "reports/${tag}_full_stdout.txt"
}

run_one config/naive_full.cfg  naive
run_one config/as2008_full.cfg as2008
run_one config/as2018_full.cfg as2018

echo
echo ">>> Compare summaries"
"${PYTHON}" - <<'PY'
import pandas as pd
from pathlib import Path

summaries = {}
for label, fname in [
    ("naive",  "naive_full_summary.csv"),
    ("as2008", "as2008_full_summary.csv"),
    ("as2018", "as2018_full_summary.csv"),
]:
    p = Path("reports") / fname
    if p.exists():
        s = pd.read_csv(p)
        summaries[label] = dict(zip(s["metric"], s["value"]))

df = pd.DataFrame(summaries).T
out = Path("reports/full_run_compare.csv")
df.to_csv(out)
print(df.to_string())
print(f"\nWritten {out}")
PY
