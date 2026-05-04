use crate::market_book::MarketBook;
use crate::own_orders::OwnOrders;
use crate::types::{OrderId, Px, Qty, Side, Ts};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Fill {
    pub ts_us: Ts,
    pub order_id: OrderId,
    pub side: Side,
    pub price: Px,
    pub qty: Qty,
}

pub struct FillEngine {
    pub allow_partial: bool,
}

impl FillEngine {
    pub fn new(allow_partial: bool) -> Self {
        Self { allow_partial }
    }

    pub fn match_book(&self, ts: Ts, book: &MarketBook, own: &mut OwnOrders) -> Vec<Fill> {
        let best_bid = book.best_bid();
        let best_ask = book.best_ask();
        let mut bid_depth = best_bid.map(|(_, q)| q).unwrap_or(0);
        let mut ask_depth = best_ask.map(|(_, q)| q).unwrap_or(0);
        let mut candidates: Vec<OrderId> = own.iter().map(|o| o.id).collect();
        candidates.sort_unstable();

        let mut fills = Vec::new();

        for id in candidates {
            let o = match own.get(id) {
                Some(x) => x.clone(),
                None => continue,
            };

            let (cross, available) = match o.side {
                Side::Bid => match best_ask {
                    Some((ap, _)) if ap.0 <= o.price.0 => (true, ask_depth),
                    _ => (false, 0),
                },
                Side::Ask => match best_bid {
                    Some((bp, _)) if bp.0 >= o.price.0 => (true, bid_depth),
                    _ => (false, 0),
                },
            };

            if !cross {
                continue;
            }

            let fill_qty = if self.allow_partial {
                o.qty.min(available)
            } else if available >= o.qty {
                o.qty
            } else {
                continue;
            };

            if fill_qty == 0 {
                continue;
            }

            fills.push(Fill {
                ts_us: ts,
                order_id: id,
                side: o.side,
                price: o.price,
                qty: fill_qty,
            });

            match o.side {
                Side::Bid => ask_depth = ask_depth.saturating_sub(fill_qty),
                Side::Ask => bid_depth = bid_depth.saturating_sub(fill_qty),
            }

            if fill_qty >= o.qty {
                own.cancel(id);
            } else {
                own.reduce_qty(id, fill_qty);
            }
        }
        fills
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::lob::BookSnapshot;
    fn px(s: &str) -> Px {
        Px(s.parse().unwrap())
    }
    fn snap(b: Vec<(&str, u64)>, a: Vec<(&str, u64)>) -> BookSnapshot {
        BookSnapshot {
            ts_us: 100,
            seq: 1,
            bids: b.into_iter().map(|(p, q)| (px(p), q)).collect(),
            asks: a.into_iter().map(|(p, q)| (px(p), q)).collect(),
        }
    }

    #[test]
    fn deterministic_order_by_id() {
        let mut mb = MarketBook::new();
        mb.apply(snap(vec![("99", 10)], vec![("100", 100)]));
        let mut o = OwnOrders::new();
        let a = o.place(Side::Bid, px("100"), 1, 1);
        let b = o.place(Side::Bid, px("100"), 1, 1);
        let c = o.place(Side::Bid, px("100"), 1, 1);
        let f = FillEngine::new(true).match_book(100, &mb, &mut o);
        assert_eq!(
            f.iter().map(|x| x.order_id).collect::<Vec<_>>(),
            vec![a, b, c]
        );
    }

    #[test]
    fn partial_fills_share_best_level_depth() {
        let mut mb = MarketBook::new();
        mb.apply(snap(vec![("99", 10)], vec![("100", 5)]));
        let mut o = OwnOrders::new();
        let first = o.place(Side::Bid, px("100"), 5, 1);
        let second = o.place(Side::Bid, px("100"), 5, 1);

        let f = FillEngine::new(true).match_book(100, &mb, &mut o);

        assert_eq!(f.len(), 1);
        assert_eq!(f[0].order_id, first);
        assert_eq!(f[0].qty, 5);
        assert!(o.get(first).is_none());
        assert_eq!(o.get(second).unwrap().qty, 5);
    }
}
