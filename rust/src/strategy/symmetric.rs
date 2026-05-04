use super::{OrderAction, Strategy, TickCtx};
use crate::fill_engine::Fill;
use crate::types::{Px, Qty, Side};
use rust_decimal::Decimal;

pub struct SymmetricMM {
    pub half_spread: Decimal, // δ (price units)
    pub quote_qty: Qty,
    pub max_abs_inventory: i64,
}

impl SymmetricMM {
    pub fn new(half_spread: Decimal, quote_qty: Qty, max_abs_inventory: i64) -> Self {
        Self {
            half_spread,
            quote_qty,
            max_abs_inventory,
        }
    }
}

impl Strategy for SymmetricMM {
    fn name(&self) -> &str {
        "symmetric"
    }

    fn on_tick(&mut self, ctx: &TickCtx) -> Vec<OrderAction> {
        let mid = match ctx.book.mid() {
            Some(m) => m,
            None => return vec![],
        };
        let mut actions = vec![OrderAction::CancelAll];
        let bid_px = Px(mid.0 - self.half_spread);
        let ask_px = Px(mid.0 + self.half_spread);

        // Inventory cap: skip the side that would push us further over
        if ctx.position < self.max_abs_inventory {
            actions.push(OrderAction::Place {
                side: Side::Bid,
                price: bid_px,
                qty: self.quote_qty,
            });
        }
        if ctx.position > -self.max_abs_inventory {
            actions.push(OrderAction::Place {
                side: Side::Ask,
                price: ask_px,
                qty: self.quote_qty,
            });
        }
        actions
    }

    fn on_fill(&mut self, _f: &Fill, _pos: i64) {}
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::market_book::MarketBook;
    use crate::own_orders::OwnOrders;
    use crate::parser::lob::BookSnapshot;

    fn px(s: &str) -> Px {
        Px(s.parse().unwrap())
    }
    fn ctx_with_mid<'a>(book: &'a MarketBook, own: &'a OwnOrders, pos: i64) -> TickCtx<'a> {
        TickCtx {
            ts_us: 0,
            book,
            own,
            position: pos,
            elapsed_ms: 0,
            session_total_ms: 1000,
        }
    }

    #[test]
    fn quotes_both_sides_when_flat() {
        let mut mb = MarketBook::new();
        mb.apply(BookSnapshot {
            ts_us: 1,
            seq: 1,
            bids: vec![(px("100"), 5)],
            asks: vec![(px("102"), 5)],
        });
        let own = OwnOrders::new();
        let mut s = SymmetricMM::new("0.5".parse().unwrap(), 1, 100);
        let acts = s.on_tick(&ctx_with_mid(&mb, &own, 0));
        let places: Vec<_> = acts
            .iter()
            .filter(|a| matches!(a, OrderAction::Place { .. }))
            .collect();
        assert_eq!(places.len(), 2);
    }

    #[test]
    fn skips_bid_when_long_at_cap() {
        let mut mb = MarketBook::new();
        mb.apply(BookSnapshot {
            ts_us: 1,
            seq: 1,
            bids: vec![(px("100"), 5)],
            asks: vec![(px("102"), 5)],
        });
        let own = OwnOrders::new();
        let mut s = SymmetricMM::new("0.5".parse().unwrap(), 1, 10);
        let acts = s.on_tick(&ctx_with_mid(&mb, &own, 10));
        let bids: Vec<_> = acts
            .iter()
            .filter(|a| {
                matches!(
                    a,
                    OrderAction::Place {
                        side: Side::Bid,
                        ..
                    }
                )
            })
            .collect();
        assert!(bids.is_empty());
    }

    #[test]
    fn empty_book_yields_no_actions() {
        let mb = MarketBook::new();
        let own = OwnOrders::new();
        let mut s = SymmetricMM::new("0.5".parse().unwrap(), 1, 10);
        assert!(s.on_tick(&ctx_with_mid(&mb, &own, 0)).is_empty());
    }
}
