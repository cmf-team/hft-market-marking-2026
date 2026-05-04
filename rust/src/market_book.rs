use crate::parser::lob::BookSnapshot;
use crate::types::{Px, Qty, Ts};
use rust_decimal::Decimal;

pub struct MarketBook {
    snap: Option<BookSnapshot>,
}

impl MarketBook {
    pub fn new() -> Self {
        Self { snap: None }
    }

    pub fn apply(&mut self, s: BookSnapshot) {
        self.snap = Some(s);
    }

    pub fn ts(&self) -> Option<Ts> {
        self.snap.as_ref().map(|s| s.ts_us)
    }

    pub fn is_ready(&self) -> bool {
        matches!(&self.snap, Some(s) if !s.bids.is_empty() && !s.asks.is_empty())
    }

    pub fn best_bid(&self) -> Option<(Px, Qty)> {
        self.snap.as_ref().and_then(|s| s.bids.first().copied())
    }

    pub fn best_ask(&self) -> Option<(Px, Qty)> {
        self.snap.as_ref().and_then(|s| s.asks.first().copied())
    }

    pub fn mid(&self) -> Option<Px> {
        let (b, _) = self.best_bid()?;
        let (a, _) = self.best_ask()?;
        Some(Px((b.0 + a.0) / Decimal::from(2)))
    }

    pub fn microprice(&self) -> Option<Px> {
        let (a, aq) = self.best_ask()?;
        let (b, bq) = self.best_bid()?;
        let aq = Decimal::from(aq);
        let bq = Decimal::from(bq);
        let denom = bq + aq;
        if denom.is_zero() {
            return self.mid();
        }
        Some(Px((a.0 * bq + b.0 * aq) / denom))
    }
}

impl Default for MarketBook {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn snap(bids: Vec<(&str, u64)>, asks: Vec<(&str, u64)>) -> BookSnapshot {
        BookSnapshot {
            ts_us: 1,
            seq: 1,
            bids: bids
                .into_iter()
                .map(|(p, q)| (Px(p.parse().unwrap()), q))
                .collect(),
            asks: asks
                .into_iter()
                .map(|(p, q)| (Px(p.parse().unwrap()), q))
                .collect(),
        }
    }
    #[test]
    fn empty_returns_none() {
        let mb = MarketBook::new();
        assert!(!mb.is_ready());
        assert!(mb.mid().is_none());
    }
    #[test]
    fn mid_average() {
        let mut mb = MarketBook::new();
        mb.apply(snap(vec![("100", 5)], vec![("102", 5)]));
        assert_eq!(mb.mid().unwrap().0, "101".parse().unwrap());
    }
    #[test]
    fn microprice_skews_toward_thick() {
        let mut mb = MarketBook::new();
        mb.apply(snap(vec![("100", 90)], vec![("102", 10)]));
        let mp = mb.microprice().unwrap().0;
        assert!(mp > "101".parse().unwrap() && mp < "102".parse().unwrap());
    }
    #[test]
    fn one_sided_not_ready() {
        let mut mb = MarketBook::new();
        mb.apply(snap(vec![("100", 5)], vec![]));
        assert!(!mb.is_ready());
    }
}
