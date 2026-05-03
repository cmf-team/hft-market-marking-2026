import sys
import os
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
from lob import Order, LimitOrderBook, Side

# ══════════════════════════════════════════════
# Unit Tests
# ══════════════════════════════════════════════


class TestOrder(unittest.TestCase):
    def test_valid_order(self):
        o = Order(1.0, 100.0, 10.0, Side.BID)
        self.assertEqual(o.price, 100.0)
        self.assertEqual(o.size, 10.0)
        self.assertEqual(o.side, Side.BID)

    def test_invalid_price(self):
        with self.assertRaises(ValueError):
            Order(1.0, -1.0, 10.0, Side.BID)

    def test_invalid_size(self):
        with self.assertRaises(ValueError):
            Order(1.0, 100.0, 0.0, Side.BID)

    def test_invalid_side_type(self):
        with self.assertRaises(TypeError):
            Order(1.0, 100.0, 10.0, "bid")

    def test_unique_order_ids(self):
        o1 = Order(1.0, 100.0, 10.0, Side.BID)
        o2 = Order(1.0, 100.0, 10.0, Side.BID)
        self.assertNotEqual(o1.order_id, o2.order_id)


class TestLOBPlacement(unittest.TestCase):
    def setUp(self):
        self.lob = LimitOrderBook()

    def test_place_bid_no_match(self):
        o = Order(1.0, 99.0, 5.0, Side.BID)
        fills = self.lob.place_order(o)
        self.assertEqual(fills, [])
        self.assertEqual(self.lob.best_bid_price(), 99.0)

    def test_place_ask_no_match(self):
        o = Order(1.0, 101.0, 5.0, Side.ASK)
        fills = self.lob.place_order(o)
        self.assertEqual(fills, [])
        self.assertEqual(self.lob.best_ask_price(), 101.0)

    def test_multiple_bid_levels_sorted(self):
        self.lob.place_order(Order(1.0, 98.0, 5.0, Side.BID))
        self.lob.place_order(Order(2.0, 99.0, 5.0, Side.BID))
        self.lob.place_order(Order(3.0, 97.0, 5.0, Side.BID))
        self.assertEqual(self.lob.best_bid_price(), 99.0)

    def test_multiple_ask_levels_sorted(self):
        self.lob.place_order(Order(1.0, 102.0, 5.0, Side.ASK))
        self.lob.place_order(Order(2.0, 101.0, 5.0, Side.ASK))
        self.lob.place_order(Order(3.0, 103.0, 5.0, Side.ASK))
        self.assertEqual(self.lob.best_ask_price(), 101.0)

    def test_spread_and_mid(self):
        self.lob.place_order(Order(1.0, 99.0, 5.0, Side.BID))
        self.lob.place_order(Order(2.0, 101.0, 5.0, Side.ASK))
        self.assertAlmostEqual(self.lob.spread(), 2.0)
        self.assertAlmostEqual(self.lob.mid_price(), 100.0)


class TestLOBExecution(unittest.TestCase):
    def setUp(self):
        self.lob = LimitOrderBook()

    def test_full_match_bid_aggressor(self):
        ask = Order(1.0, 100.0, 10.0, Side.ASK)
        self.lob.place_order(ask)
        bid = Order(2.0, 100.0, 10.0, Side.BID)
        fills = self.lob.place_order(bid)
        self.assertEqual(len(fills), 1)
        self.assertAlmostEqual(fills[0].fill_size, 10.0)
        self.assertAlmostEqual(fills[0].fill_price, 100.0)
        self.assertIsNone(self.lob.best_ask_price())
        self.assertIsNone(self.lob.best_bid_price())

    def test_full_match_ask_aggressor(self):
        bid = Order(1.0, 100.0, 10.0, Side.BID)
        self.lob.place_order(bid)
        ask = Order(2.0, 100.0, 10.0, Side.ASK)
        fills = self.lob.place_order(ask)
        self.assertEqual(len(fills), 1)
        self.assertAlmostEqual(fills[0].fill_size, 10.0)

    def test_partial_fill_bid_aggressor(self):
        ask = Order(1.0, 100.0, 4.0, Side.ASK)
        self.lob.place_order(ask)
        bid = Order(2.0, 100.0, 10.0, Side.BID)
        fills = self.lob.place_order(bid)
        self.assertEqual(len(fills), 1)
        self.assertAlmostEqual(fills[0].fill_size, 4.0)
        # Remainder should rest on bid side
        self.assertAlmostEqual(self.lob.best_bid_price(), 100.0)
        self.assertAlmostEqual(self.lob.bid_depth()[100.0], 6.0)

    def test_partial_fill_resting_order(self):
        ask = Order(1.0, 100.0, 10.0, Side.ASK)
        self.lob.place_order(ask)
        bid = Order(2.0, 100.0, 4.0, Side.BID)
        fills = self.lob.place_order(bid)
        self.assertAlmostEqual(fills[0].fill_size, 4.0)
        # Resting ask should have 6 remaining
        self.assertAlmostEqual(self.lob.ask_depth()[100.0], 6.0)

    def test_no_match_bid_below_ask(self):
        self.lob.place_order(Order(1.0, 101.0, 10.0, Side.ASK))
        fills = self.lob.place_order(Order(2.0, 99.0, 10.0, Side.BID))
        self.assertEqual(fills, [])
        self.assertAlmostEqual(self.lob.best_bid_price(), 99.0)
        self.assertAlmostEqual(self.lob.best_ask_price(), 101.0)

    def test_fill_price_is_passive_price(self):
        self.lob.place_order(Order(1.0, 100.0, 10.0, Side.ASK))
        fills = self.lob.place_order(Order(2.0, 105.0, 10.0, Side.BID))
        self.assertAlmostEqual(fills[0].fill_price, 100.0)  # passive ask price

    def test_fifo_priority(self):
        a1 = Order(1.0, 100.0, 3.0, Side.ASK)
        a2 = Order(2.0, 100.0, 3.0, Side.ASK)
        self.lob.place_order(a1)
        self.lob.place_order(a2)
        fills = self.lob.place_order(Order(3.0, 100.0, 3.0, Side.BID))
        self.assertEqual(len(fills), 1)
        self.assertEqual(fills[0].ask_order_id, a1.order_id)  # a1 first

    def test_sweep_multiple_levels(self):
        self.lob.place_order(Order(1.0, 100.0, 5.0, Side.ASK))
        self.lob.place_order(Order(2.0, 101.0, 5.0, Side.ASK))
        fills = self.lob.place_order(Order(3.0, 102.0, 10.0, Side.BID))
        self.assertEqual(len(fills), 2)
        total_filled = sum(f.fill_size for f in fills)
        self.assertAlmostEqual(total_filled, 10.0)
        self.assertIsNone(self.lob.best_ask_price())


class TestLOBCancellation(unittest.TestCase):
    def setUp(self):
        self.lob = LimitOrderBook()

    def test_cancel_existing_order(self):
        o = Order(1.0, 99.0, 5.0, Side.BID)
        self.lob.place_order(o)
        cancelled = self.lob.cancel_order(o.order_id)
        self.assertIsNotNone(cancelled)
        self.assertEqual(cancelled.order_id, o.order_id)
        self.assertIsNone(self.lob.best_bid_price())

    def test_cancel_nonexistent_order(self):
        result = self.lob.cancel_order("nonexistent-id")
        self.assertIsNone(result)

    def test_cancel_cleans_price_level(self):
        o = Order(1.0, 99.0, 5.0, Side.BID)
        self.lob.place_order(o)
        self.lob.cancel_order(o.order_id)
        self.assertNotIn(99.0, self.lob.bid_depth())

    def test_cancel_one_of_two_orders_at_level(self):
        o1 = Order(1.0, 99.0, 5.0, Side.BID)
        o2 = Order(2.0, 99.0, 3.0, Side.BID)
        self.lob.place_order(o1)
        self.lob.place_order(o2)
        self.lob.cancel_order(o1.order_id)
        self.assertAlmostEqual(self.lob.bid_depth()[99.0], 3.0)

    def test_cancel_already_filled_order(self):
        ask = Order(1.0, 100.0, 5.0, Side.ASK)
        self.lob.place_order(ask)
        self.lob.place_order(Order(2.0, 100.0, 5.0, Side.BID))
        # ask is now fully filled and removed from index
        result = self.lob.cancel_order(ask.order_id)
        self.assertIsNone(result)

    def test_cancel_partially_filled_resting_order(self):
        ask = Order(1.0, 100.0, 10.0, Side.ASK)
        self.lob.place_order(ask)
        self.lob.place_order(Order(2.0, 100.0, 4.0, Side.BID))
        cancelled = self.lob.cancel_order(ask.order_id)
        self.assertIsNotNone(cancelled)
        self.assertAlmostEqual(cancelled.size, 6.0)  # partial remaining

    def test_order_not_exists_after_cancel(self):
        o = Order(1.0, 99.0, 5.0, Side.BID)
        self.lob.place_order(o)
        self.lob.cancel_order(o.order_id)
        self.assertFalse(self.lob.order_exists(o.order_id))


class TestLOBDepth(unittest.TestCase):
    def setUp(self):
        self.lob = LimitOrderBook()

    def test_bid_depth_aggregation(self):
        self.lob.place_order(Order(1.0, 99.0, 3.0, Side.BID))
        self.lob.place_order(Order(2.0, 99.0, 7.0, Side.BID))
        self.assertAlmostEqual(self.lob.bid_depth()[99.0], 10.0)

    def test_empty_book_no_spread(self):
        self.assertIsNone(self.lob.spread())
        self.assertIsNone(self.lob.mid_price())


if __name__ == "__main__":
    unittest.main(verbosity=2)
