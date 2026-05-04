use crate::fill_engine::Fill;
use crate::types::{Px, Side, Ts};
use rust_decimal::Decimal;

#[derive(Default, Debug, Clone)]
pub struct Metrics {
    pub cash: Decimal,
    pub position: i64,
    pub max_abs_position: i64,
    pub turnover_notional: Decimal,
    pub trade_count: u64,
    pub buys: u64,
    pub sells: u64,
    pub equity_curve: Vec<EquityPoint>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EquityPoint {
    pub ts_us: Ts,
    pub mid: Px,
    pub equity: Decimal,
    pub position: i64,
}

impl Metrics {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn on_fill(&mut self, f: &Fill) {
        let qty = Decimal::from(f.qty);
        let notional = f.price.0 * qty;
        match f.side {
            Side::Bid => {
                self.cash -= notional;
                self.position += f.qty as i64;
                self.buys += 1;
            }
            Side::Ask => {
                self.cash += notional;
                self.position -= f.qty as i64;
                self.sells += 1;
            }
        }
        self.max_abs_position = self.max_abs_position.max(self.position.abs());
        self.turnover_notional += notional;
        self.trade_count += 1;
    }

    pub fn equity(&self, mid: Px) -> Decimal {
        self.cash + Decimal::from(self.position) * mid.0
    }

    pub fn sample(&mut self, ts: Ts, mid: Px) {
        let eq = self.equity(mid);
        self.max_abs_position = self.max_abs_position.max(self.position.abs());
        self.equity_curve.push(EquityPoint {
            ts_us: ts,
            mid,
            equity: eq,
            position: self.position,
        });
    }

    pub fn summary(&self, final_mid: Option<Px>) -> Summary {
        let final_equity = final_mid.map(|m| self.equity(m));
        Summary {
            final_equity,
            final_position: self.position,
            max_abs_position: self.max_abs_position,
            turnover_notional: self.turnover_notional,
            trade_count: self.trade_count,
            buys: self.buys,
            sells: self.sells,
        }
    }
}

#[derive(Debug, Clone)]
pub struct Summary {
    pub final_equity: Option<Decimal>,
    pub final_position: i64,
    pub max_abs_position: i64,
    pub turnover_notional: Decimal,
    pub trade_count: u64,
    pub buys: u64,
    pub sells: u64,
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::types::Side;
    fn px(s: &str) -> Px {
        Px(s.parse().unwrap())
    }

    #[test]
    fn buy_then_sell_realizes_pnl() {
        let mut m = Metrics::new();
        m.on_fill(&Fill {
            ts_us: 1,
            order_id: 1,
            side: Side::Bid,
            price: px("100"),
            qty: 10,
        });
        assert_eq!(m.position, 10);
        m.on_fill(&Fill {
            ts_us: 2,
            order_id: 2,
            side: Side::Ask,
            price: px("101"),
            qty: 10,
        });
        assert_eq!(m.position, 0);
        assert_eq!(m.cash, "10".parse().unwrap());
        assert_eq!(m.equity(px("100")), "10".parse().unwrap());
    }
    #[test]
    fn turnover_accumulates_gross() {
        let mut m = Metrics::new();
        m.on_fill(&Fill {
            ts_us: 1,
            order_id: 1,
            side: Side::Bid,
            price: px("100"),
            qty: 5,
        });
        m.on_fill(&Fill {
            ts_us: 2,
            order_id: 2,
            side: Side::Ask,
            price: px("99"),
            qty: 5,
        });
        assert_eq!(m.turnover_notional, "995".parse().unwrap());
    }
    #[test]
    fn equity_curve_records_samples() {
        let mut m = Metrics::new();
        m.sample(1, px("100"));
        m.on_fill(&Fill {
            ts_us: 2,
            order_id: 1,
            side: Side::Bid,
            price: px("100"),
            qty: 1,
        });
        m.sample(3, px("101"));
        assert_eq!(m.equity_curve.len(), 2);
        assert_eq!(m.equity_curve[1].equity, "1".parse().unwrap());
    }
    #[test]
    fn summary_tracks_max_abs_position() {
        let mut m = Metrics::new();
        m.sample(1, px("100"));
        m.position = 5;
        m.sample(2, px("100"));
        m.position = -7;
        m.sample(3, px("100"));
        m.position = 3;
        m.sample(4, px("100"));
        assert_eq!(m.summary(Some(px("100"))).max_abs_position, 7);
    }

    #[test]
    fn summary_tracks_fill_time_position_peaks() {
        let mut m = Metrics::new();
        m.sample(1, px("100"));
        m.on_fill(&Fill {
            ts_us: 2,
            order_id: 1,
            side: Side::Bid,
            price: px("100"),
            qty: 10,
        });
        m.on_fill(&Fill {
            ts_us: 3,
            order_id: 2,
            side: Side::Ask,
            price: px("100"),
            qty: 10,
        });
        m.sample(4, px("100"));

        assert_eq!(m.summary(Some(px("100"))).max_abs_position, 10);
    }
}
