"""
L2 snapshot reader for LimitOrderBook.

Each CSV row is a full order-book snapshot with up to 25 price levels per side.
The reconciler diffs consecutive snapshots: it cancels any level whose price or
size changed, then places a fresh synthetic order for the new state.

Because L2 data never has a crossed book, placed orders will not trigger fills.
"""

from __future__ import annotations

import csv
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterator

from lob import Fill, LimitOrderBook, Order, Side

LEVELS = 25


@dataclass
class Snapshot:
    timestamp: int  # microseconds (raw)
    bids: dict[float, float] = field(default_factory=dict)  # price -> size
    asks: dict[float, float] = field(default_factory=dict)

    @classmethod
    def from_row(cls, row: dict) -> "Snapshot":
        snap = cls(timestamp=int(row["local_timestamp"]))
        for i in range(LEVELS):
            bp, ba = float(row[f"bids[{i}].price"]), float(row[f"bids[{i}].amount"])
            ap, aa = float(row[f"asks[{i}].price"]), float(row[f"asks[{i}].amount"])
            if ba > 0:
                snap.bids[bp] = ba
            if aa > 0:
                snap.asks[ap] = aa
        return snap


class LOBReader:
    """
    Ingests L2 snapshots into a LimitOrderBook one row at a time.

    For each snapshot it:
      1. Cancels synthetic orders whose level price/size changed or disappeared.
      2. Places new synthetic orders for levels that are new or changed.

    One synthetic order per (side, price) level is maintained in _level_orders.
    """

    def __init__(self, lob: LimitOrderBook) -> None:
        self._lob = lob
        self._level_orders: dict[
            tuple[Side, float], str
        ] = {}  # (side, price) -> order_id
        self._prev: Snapshot | None = None

    def ingest(self, snap: Snapshot) -> list[Fill]:
        """Apply one snapshot to the LOB. Returns fills (normally empty)."""
        ts = snap.timestamp / 1e6  # µs -> s

        if self._prev is None:
            self._bootstrap(snap, ts)
        else:
            self._reconcile(snap, ts)

        self._prev = snap
        return list(
            self._lob.fills[-len(self._lob.fills) :]
        )  # caller rarely needs these

    def ingest_csv(self, path: str | Path) -> Iterator[tuple[Snapshot, list[Fill]]]:
        """Yield (snapshot, fills) for every row in the CSV."""
        fill_cursor = 0
        with open(path, newline="") as fh:
            for row in csv.DictReader(fh):
                snap = Snapshot.from_row(row)
                self.ingest(snap)
                new_fills = self._lob.fills[fill_cursor:]
                fill_cursor = len(self._lob.fills)
                yield snap, list(new_fills)

    def _bootstrap(self, snap: Snapshot, ts: float) -> None:
        """Populate LOB from first snapshot without triggering matching."""
        # Place asks first (high prices), then bids (low prices) — no crosses possible.
        for price, size in snap.asks.items():
            self._place(Side.ASK, price, size, ts)
        for price, size in snap.bids.items():
            self._place(Side.BID, price, size, ts)

    def _reconcile(self, snap: Snapshot, ts: float) -> None:
        prev = self._prev
        assert prev is not None

        # ── step 1: cancel levels that disappeared or changed ─────────────────
        for price, old_size in prev.bids.items():
            if price not in snap.bids or snap.bids[price] != old_size:
                self._cancel(Side.BID, price)

        for price, old_size in prev.asks.items():
            if price not in snap.asks or snap.asks[price] != old_size:
                self._cancel(Side.ASK, price)

        # ── step 2: place orders for new or changed levels ────────────────────
        for price, size in snap.bids.items():
            if prev.bids.get(price) != size:
                self._place(Side.BID, price, size, ts)

        for price, size in snap.asks.items():
            if prev.asks.get(price) != size:
                self._place(Side.ASK, price, size, ts)

    def _place(self, side: Side, price: float, size: float, ts: float) -> None:
        order = Order(ts, price, size, side)
        self._lob.place_order(order)
        self._level_orders[(side, price)] = order.order_id

    def _cancel(self, side: Side, price: float) -> None:
        key = (side, price)
        oid = self._level_orders.pop(key, None)
        if oid is not None:
            self._lob.cancel_order(oid)
