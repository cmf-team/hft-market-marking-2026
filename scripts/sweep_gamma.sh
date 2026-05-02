#!/usr/bin/env bash
# scripts/sweep_gamma.sh -- параметрический свип gamma на полном датасете.
# Использует CLI-переопределения, чтобы не плодить cfg-файлы.
#
# Параметры (env): GAMMAS, ORDER_SIZES, STRATEGY, LOB, TRADES.
# Запускать из корня репозитория.

set -euo pipefail
cd "$(dirname "$0")/.."

GAMMAS="${GAMMAS:-0.01 0.05 0.1 0.5 1.0}"
ORDER_SIZES="${ORDER_SIZES:-100 1000 10000}"
STRATEGY="${STRATEGY:-as2008}"
MAX_INV="${MAX_INV:-50000}"
T_SEC="${T_SEC:-86400}"
SIGMA_WIN="${SIGMA_WIN:-500}"
LOB="${LOB:-MD/lob.csv}"
TRADES="${TRADES:-MD/trades.csv}"
BIN="build/bin/hft-market-making"

mkdir -p reports/sweep
OUT="reports/sweep/${STRATEGY}_grid.csv"
echo "strategy,gamma,order_size,total_pnl,realized_pnl,unrealized_pnl,max_drawdown,total_volume,total_turnover,orders_filled,fill_rate,calmar_ratio,final_inventory" > "${OUT}"

for g in ${GAMMAS}; do
  for sz in ${ORDER_SIZES}; do
    tag="${STRATEGY}_g${g}_q${sz}"
    summary="reports/sweep/${tag}_summary.csv"
    ts="reports/sweep/${tag}_timeseries.csv"
    echo "  >>> ${tag}"
    "${BIN}" \
        --lob "${LOB}" --trades "${TRADES}" \
        --no-trades \
        --strategy "${STRATEGY}" \
        --gamma "${g}" \
        --order-size "${sz}" \
        --max-inventory "${MAX_INV}" \
        --sigma-window "${SIGMA_WIN}" \
        --T-seconds "${T_SEC}" \
        --summary "${summary}" \
        --timeseries "${ts}" \
        > "reports/sweep/${tag}_stdout.txt" 2>&1 \
      || { echo "    FAILED"; continue; }

    python3 - "${STRATEGY}" "${g}" "${sz}" "${summary}" "${OUT}" <<'PY'
import csv, sys
strategy, g, sz, summary_path, out_path = sys.argv[1:]
m = {row['metric']: row['value'] for row in csv.DictReader(open(summary_path))}
get = lambda k: m.get(k, '')
with open(out_path, 'a') as f:
    f.write(f"{strategy},{g},{sz},{get('total_pnl')},{get('realized_pnl')},"
            f"{get('unrealized_pnl')},{get('max_drawdown')},"
            f"{get('total_volume')},{get('total_turnover')},"
            f"{get('orders_filled')},{get('fill_rate')},{get('calmar_ratio')},"
            f"{get('final_inventory')}\n")
PY
  done
done

echo
echo ">>> Sweep grid written to ${OUT}"
column -ts, "${OUT}"
