//! Property tests for the crossing-rule `FillEngine`.
//!
//! Invariants we care about (from §6 of the design doc):
//!   1. No fill when there is no cross.
//!   2. Fill quantity is bounded by both the order's qty and the depth at the
//!      best opposite level.
//!   3. The fill price is always the resting limit (not the touch).
//!   4. With `allow_partial=false`, a fill is either zero or the full order qty.
//!   5. Conservation: original order qty = fill qty + remaining qty.
//!   6. Determinism: same inputs produce identical outputs.

use hft_mm_backtester::fill_engine::FillEngine;
use hft_mm_backtester::market_book::MarketBook;
use hft_mm_backtester::own_orders::OwnOrders;
use hft_mm_backtester::parser::lob::BookSnapshot;
use hft_mm_backtester::types::{Px, Side};
use proptest::prelude::*;
use rust_decimal::Decimal;

fn px_from_int(n: i64) -> Px {
    Px(Decimal::from(n))
}

/// Build a single-level book at integer prices.
fn book_at(bid_px: i64, bid_qty: u64, ask_px: i64, ask_qty: u64) -> BookSnapshot {
    let mut bids = Vec::new();
    let mut asks = Vec::new();
    if bid_qty > 0 {
        bids.push((px_from_int(bid_px), bid_qty));
    }
    if ask_qty > 0 {
        asks.push((px_from_int(ask_px), ask_qty));
    }
    BookSnapshot {
        ts_us: 1,
        seq: 1,
        bids,
        asks,
    }
}

proptest! {
    /// Buy order never fills when the best ask is strictly above our limit.
    #[test]
    fn bid_no_cross_no_fill(
        bid_qty in 1u64..1_000,
        ask_qty in 1u64..1_000,
        my_bid in 0i64..50,
        gap in 1i64..20,
        order_qty in 1u64..1_000,
    ) {
        let ask_px = my_bid + gap; // strictly above our limit
        let mut mb = MarketBook::new();
        mb.apply(book_at(my_bid - 10, bid_qty, ask_px, ask_qty));
        let mut own = OwnOrders::new();
        own.place(Side::Bid, px_from_int(my_bid), order_qty, 1);

        let fills = FillEngine::new(true).match_book(1, &mb, &mut own);
        prop_assert!(fills.is_empty());
        prop_assert_eq!(own.len(), 1);
    }

    /// Sell order never fills when the best bid is strictly below our limit.
    #[test]
    fn ask_no_cross_no_fill(
        bid_qty in 1u64..1_000,
        ask_qty in 1u64..1_000,
        my_ask in 10i64..50,
        gap in 1i64..9,
        order_qty in 1u64..1_000,
    ) {
        let bid_px = my_ask - gap; // strictly below our limit
        let mut mb = MarketBook::new();
        mb.apply(book_at(bid_px, bid_qty, my_ask + 10, ask_qty));
        let mut own = OwnOrders::new();
        own.place(Side::Ask, px_from_int(my_ask), order_qty, 1);

        let fills = FillEngine::new(true).match_book(1, &mb, &mut own);
        prop_assert!(fills.is_empty());
        prop_assert_eq!(own.len(), 1);
    }

    /// On a cross, the fill quantity is exactly `min(order_qty, depth)` and
    /// the fill price is the resting limit (not the touch).
    #[test]
    fn bid_cross_fill_qty_and_price(
        order_qty in 1u64..1_000,
        depth in 1u64..1_000,
        my_bid in 5i64..100,
        ask_offset in 0i64..5,        // 0 ⇒ equality cross, >0 ⇒ strict cross
    ) {
        let ask_px = my_bid - ask_offset; // ≤ my_bid ⇒ cross
        let mut mb = MarketBook::new();
        mb.apply(book_at(my_bid - 10, 999, ask_px, depth));
        let mut own = OwnOrders::new();
        let id = own.place(Side::Bid, px_from_int(my_bid), order_qty, 1);

        let fills = FillEngine::new(true).match_book(1, &mb, &mut own);
        prop_assert_eq!(fills.len(), 1);
        let f = &fills[0];
        prop_assert_eq!(f.order_id, id);
        prop_assert_eq!(f.side, Side::Bid);
        prop_assert_eq!(f.price, px_from_int(my_bid));
        prop_assert_eq!(f.qty, order_qty.min(depth));

        // Conservation: filled + remaining == original.
        let remaining = own.get(id).map(|o| o.qty).unwrap_or(0);
        prop_assert_eq!(f.qty + remaining, order_qty);
    }

    /// Symmetric property for sells.
    #[test]
    fn ask_cross_fill_qty_and_price(
        order_qty in 1u64..1_000,
        depth in 1u64..1_000,
        my_ask in 5i64..100,
        bid_offset in 0i64..5,
    ) {
        let bid_px = my_ask + bid_offset; // ≥ my_ask ⇒ cross
        let mut mb = MarketBook::new();
        mb.apply(book_at(bid_px, depth, my_ask + 10, 999));
        let mut own = OwnOrders::new();
        let id = own.place(Side::Ask, px_from_int(my_ask), order_qty, 1);

        let fills = FillEngine::new(true).match_book(1, &mb, &mut own);
        prop_assert_eq!(fills.len(), 1);
        let f = &fills[0];
        prop_assert_eq!(f.order_id, id);
        prop_assert_eq!(f.side, Side::Ask);
        prop_assert_eq!(f.price, px_from_int(my_ask));
        prop_assert_eq!(f.qty, order_qty.min(depth));

        let remaining = own.get(id).map(|o| o.qty).unwrap_or(0);
        prop_assert_eq!(f.qty + remaining, order_qty);
    }

    /// With `allow_partial=false`, a cross either fills the whole order or
    /// produces no fill at all — never a partial.
    #[test]
    fn no_partial_when_disabled(
        order_qty in 1u64..1_000,
        depth in 1u64..1_000,
        my_bid in 5i64..100,
    ) {
        let mut mb = MarketBook::new();
        mb.apply(book_at(my_bid - 10, 999, my_bid, depth));
        let mut own = OwnOrders::new();
        let id = own.place(Side::Bid, px_from_int(my_bid), order_qty, 1);

        let fills = FillEngine::new(false).match_book(1, &mb, &mut own);
        if depth >= order_qty {
            prop_assert_eq!(fills.len(), 1);
            prop_assert_eq!(fills[0].qty, order_qty);
            prop_assert!(own.get(id).is_none());
        } else {
            prop_assert!(fills.is_empty());
            prop_assert_eq!(own.get(id).unwrap().qty, order_qty);
        }
    }

    /// Determinism: identical inputs yield identical fills.
    #[test]
    fn deterministic(
        order_qty in 1u64..500,
        depth in 1u64..500,
        my_bid in 5i64..50,
    ) {
        let snap = book_at(my_bid - 10, 999, my_bid, depth);

        let run = || {
            let mut mb = MarketBook::new();
            mb.apply(snap.clone());
            let mut own = OwnOrders::new();
            own.place(Side::Bid, px_from_int(my_bid), order_qty, 1);
            FillEngine::new(true).match_book(1, &mb, &mut own)
        };
        let a = run();
        let b = run();
        prop_assert_eq!(a, b);
    }
}
