"""
Limit Order Book (LOB) Simulation
Supports: placement, cancellation, execution, partial fills.
Data format: timestamp, price, size, side ('bid' | 'ask')
"""

import uuid
from dataclasses import dataclass, field
from enum import Enum
from sortedcontainers import SortedDict
from typing import Optional


class Side(Enum):
    BID = "bid"
    ASK = "ask"


@dataclass
class Order:
    timestamp: float
    price: float
    size: float
    side: Side
    order_id: str = field(default_factory=lambda: str(uuid.uuid4()))

    def __post_init__(self):
        if self.price <= 0:
            raise ValueError(f"Price must be positive, got {self.price}")
        if self.size <= 0:
            raise ValueError(f"Size must be positive, got {self.size}")
        if not isinstance(self.side, Side):
            raise TypeError(f"Side must be Side enum, got {type(self.side)}")


@dataclass
class Fill:
    timestamp: float
    bid_order_id: str
    ask_order_id: str
    fill_price: float
    fill_size: float


class PriceLevel:
    """
    FIFO queue of orders at a single price level.
    """

    def __init__(self, price: float):
        self.price: float = price
        self.orders: list[Order] = []  # FIFO

    def add(self, order: Order) -> None:
        self.orders.append(order)

    def cancel(self, order_id: str) -> Optional[Order]:
        for i, o in enumerate(self.orders):
            if o.order_id == order_id:
                return self.orders.pop(i)
        return None

    def total_size(self) -> float:
        return sum(o.size for o in self.orders)

    def is_empty(self) -> bool:
        return len(self.orders) == 0


class LimitOrderBook:
    """
    Full LOB simulation.

    Bid side: sorted descending by price (best bid = highest).
    Ask side: sorted ascending  by price (best ask = lowest).

    All prices keyed as positive floats. Bid side uses negated keys
    in SortedDict to achieve descending order via ascending iteration.
    """

    def __init__(self):
        # SortedDict: neg_price -> PriceLevel  (bids, best = lowest neg_price)
        self._bids: SortedDict = SortedDict()
        # SortedDict: price -> PriceLevel      (asks, best = lowest price)
        self._asks: SortedDict = SortedDict()

        # order_id -> (side, price)  for O(1) cancel lookup
        self._order_index: dict[str, tuple[Side, float]] = {}

        self.fills: list[Fill] = []
        self.cancelled_orders: list[Order] = []

    # Internal helpers

    def _bid_key(self, price: float) -> float:
        return -price

    def _get_level(self, side: Side, price: float) -> Optional[PriceLevel]:
        book = self._bids if side == Side.BID else self._asks
        key = self._bid_key(price) if side == Side.BID else price
        return book.get(key)

    def _get_or_create_level(self, side: Side, price: float) -> PriceLevel:
        book = self._bids if side == Side.BID else self._asks
        key = self._bid_key(price) if side == Side.BID else price
        if key not in book:
            book[key] = PriceLevel(price)
        return book[key]

    def _remove_level_if_empty(self, side: Side, price: float) -> None:
        book = self._bids if side == Side.BID else self._asks
        key = self._bid_key(price) if side == Side.BID else price
        if key in book and book[key].is_empty():
            del book[key]

    def _best_bid(self) -> Optional[PriceLevel]:
        if not self._bids:
            return None
        return self._bids.peekitem(0)[1]  # lowest neg_price = highest price

    def _best_ask(self) -> Optional[PriceLevel]:
        if not self._asks:
            return None
        return self._asks.peekitem(0)[1]  # lowest price

    # Public API

    def place_order(self, order: Order) -> list[Fill]:
        """
        Place a limit order. Attempt matching first, then rest remainder.
        Returns list of fills generated.
        """
        new_fills: list[Fill] = []
        remaining = order.size

        if order.side == Side.BID:
            # Match against asks: consume while ask_price <= bid_price
            while remaining > 0:
                best = self._best_ask()
                if best is None or best.price > order.price:
                    break
                remaining, level_fills = self._match(
                    order, remaining, best, order.timestamp
                )
                new_fills.extend(level_fills)
                self._remove_level_if_empty(Side.ASK, best.price)

        else:  # ASK
            # Match against bids: consume while bid_price >= ask_price
            while remaining > 0:
                best = self._best_bid()
                if best is None or best.price < order.price:
                    break
                remaining, level_fills = self._match(
                    order, remaining, best, order.timestamp
                )
                new_fills.extend(level_fills)
                self._remove_level_if_empty(Side.BID, best.price)

        # Rest unfilled remainder as passive limit order
        if remaining > 0:
            order.size = remaining
            level = self._get_or_create_level(order.side, order.price)
            level.add(order)
            self._order_index[order.order_id] = (order.side, order.price)

        self.fills.extend(new_fills)
        return new_fills

    def _match(
        self,
        aggressor: Order,
        remaining: float,
        level: PriceLevel,
        timestamp: float,
    ) -> tuple[float, list[Fill]]:
        """
        Match aggressor against all resting orders in level (FIFO).
        Modifies resting order sizes in-place (partial fill support).
        Returns (remaining_size, fills).
        """
        fills: list[Fill] = []

        for resting in list(level.orders):
            if remaining <= 0:
                break

            traded = min(remaining, resting.size)
            fill_price = resting.price  # passive order sets price

            if aggressor.side == Side.BID:
                bid_id, ask_id = aggressor.order_id, resting.order_id
            else:
                bid_id, ask_id = resting.order_id, aggressor.order_id

            fills.append(
                Fill(
                    timestamp=timestamp,
                    bid_order_id=bid_id,
                    ask_order_id=ask_id,
                    fill_price=fill_price,
                    fill_size=traded,
                )
            )

            remaining -= traded
            resting.size -= traded

            if resting.size == 0:
                level.orders.remove(resting)
                self._order_index.pop(resting.order_id, None)

        return remaining, fills

    def cancel_order(self, order_id: str) -> Optional[Order]:
        """
        Cancel an order by ID. Returns the cancelled Order or None if not found.
        """
        if order_id not in self._order_index:
            return None

        side, price = self._order_index.pop(order_id)
        level = self._get_level(side, price)
        if level is None:
            return None

        cancelled = level.cancel(order_id)
        if cancelled:
            self.cancelled_orders.append(cancelled)
            self._remove_level_if_empty(side, price)
        return cancelled

    # Query API

    def best_bid_price(self) -> Optional[float]:
        lvl = self._best_bid()
        return lvl.price if lvl else None

    def best_ask_price(self) -> Optional[float]:
        lvl = self._best_ask()
        return lvl.price if lvl else None

    def mid_price(self) -> Optional[float]:
        b, a = self.best_bid_price(), self.best_ask_price()
        if b is None or a is None:
            return None
        return (b + a) / 2.0

    def spread(self) -> Optional[float]:
        b, a = self.best_bid_price(), self.best_ask_price()
        if b is None or a is None:
            return None
        return a - b

    def bid_depth(self) -> dict[float, float]:
        """price -> total_size for all bid levels."""
        return {lvl.price: lvl.total_size() for lvl in self._bids.values()}

    def ask_depth(self) -> dict[float, float]:
        """price -> total_size for all ask levels."""
        return {lvl.price: lvl.total_size() for lvl in self._asks.values()}

    def order_exists(self, order_id: str) -> bool:
        return order_id in self._order_index
