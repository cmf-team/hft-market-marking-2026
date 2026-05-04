use crate::fill_engine::Fill;
use crate::market_book::MarketBook;
use crate::own_orders::OwnOrders;
use crate::types::{OrderId, Px, Qty, Side, Ts};

pub mod avellaneda_stoikov;
pub mod symmetric;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum OrderAction {
    Place { side: Side, price: Px, qty: Qty },
    Cancel { id: OrderId },
    CancelAllSide { side: Side },
    CancelAll,
}

pub struct TickCtx<'a> {
    pub ts_us: Ts,
    pub book: &'a MarketBook,
    pub own: &'a OwnOrders,
    pub position: i64,
    pub elapsed_ms: u64,       // milliseconds since session start
    pub session_total_ms: u64, // for terminal-time τ = (T - t)
}

pub trait Strategy {
    fn name(&self) -> &str;
    /// Called once before the first tick
    fn on_start(&mut self, _ctx: &TickCtx) {}
    /// Called every tick.  Return list of order actions
    fn on_tick(&mut self, ctx: &TickCtx) -> Vec<OrderAction>;
    /// Called immediately after each fill
    fn on_fill(&mut self, _fill: &Fill, _position: i64) {}
}
