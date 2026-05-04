use super::{OrderAction, Strategy, TickCtx};
use crate::fill_engine::Fill;
use crate::types::{Px, Qty, Side};
use rust_decimal::prelude::ToPrimitive;
use rust_decimal::Decimal;

#[derive(Debug, Clone)]
pub struct AsParams {
    pub sigma: f64, // volatility (price units per √time, time = ticks)
    pub k: f64,     // intensity decay
    pub a: f64,     // intensity baseline (informational)
    pub gamma: f64, // risk aversion
    pub quote_qty: Qty,
    pub max_abs_inventory: i64,
}

pub struct AvellanedaStoikov {
    p: AsParams,
}

impl AvellanedaStoikov {
    pub fn new(p: AsParams) -> Self {
        Self { p }
    }

    /// Returns (reservation_price, half_spread)
    pub fn compute(&self, mid: f64, position: i64, tau: f64) -> (f64, f64) {
        let q = position as f64;
        let s2 = self.p.sigma * self.p.sigma;
        let res = mid - q * self.p.gamma * s2 * tau;
        let half = 0.5 * self.p.gamma * s2 * tau
            + (1.0 / self.p.gamma) * (1.0 + self.p.gamma / self.p.k).ln();
        (res, half)
    }
}

impl Strategy for AvellanedaStoikov {
    fn name(&self) -> &str {
        "avellaneda_stoikov"
    }

    fn on_tick(&mut self, ctx: &TickCtx) -> Vec<OrderAction> {
        let mid = match ctx.book.mid() {
            Some(m) => m,
            None => return vec![],
        };
        
        let mid_f = mid.0.to_f64().unwrap_or(0.0);
        let total = ctx.session_total_ms.max(1) as f64;
        let elapsed = (ctx.elapsed_ms as f64).min(total);
        let tau = (total - elapsed) / total; // 1.0 → 0.0
        let (r, half) = self.compute(mid_f, ctx.position, tau);

        let bid_f = r - half;
        let ask_f = r + half;

        let bid = match Decimal::try_from(bid_f) {
            Ok(d) => d,
            Err(_) => return vec![],
        };
        let ask = match Decimal::try_from(ask_f) {
            Ok(d) => d,
            Err(_) => return vec![],
        };

        let mut acts = vec![OrderAction::CancelAll];
        
        if ctx.position < self.p.max_abs_inventory {
            acts.push(OrderAction::Place {
                side: Side::Bid,
                price: Px(bid),
                qty: self.p.quote_qty,
            });
        }
        
        if ctx.position > -self.p.max_abs_inventory {
            acts.push(OrderAction::Place {
                side: Side::Ask,
                price: Px(ask),
                qty: self.p.quote_qty,
            });
        }
        acts
    }

    fn on_fill(&mut self, _f: &Fill, _pos: i64) {}
}
