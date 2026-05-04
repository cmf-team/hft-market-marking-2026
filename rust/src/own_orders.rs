use crate::types::{OrderId, Px, Qty, Side, Ts};
use std::collections::HashMap;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct OwnOrder {
    pub id: OrderId,
    pub side: Side,
    pub price: Px,
    pub qty: Qty,
    pub placed_ts: Ts,
}

pub struct OwnOrders {
    next_id: OrderId,
    orders: HashMap<OrderId, OwnOrder>,
}

impl OwnOrders {
    pub fn new() -> Self {
        Self {
            next_id: 1,
            orders: HashMap::new(),
        }
    }

    pub fn place(&mut self, side: Side, price: Px, qty: Qty, ts: Ts) -> OrderId {
        let id = self.next_id;
        self.next_id += 1;
        self.orders.insert(
            id,
            OwnOrder {
                id,
                side,
                price,
                qty,
                placed_ts: ts,
            },
        );
        id
    }

    pub fn cancel(&mut self, id: OrderId) -> Option<OwnOrder> {
        self.orders.remove(&id)
    }

    pub fn cancel_all_side(&mut self, side: Side) -> Vec<OwnOrder> {
        let ids: Vec<_> = self
            .orders
            .iter()
            .filter_map(|(id, o)| (o.side == side).then_some(*id))
            .collect();

        ids.into_iter()
            .filter_map(|id| self.orders.remove(&id))
            .collect()
    }

    pub fn replace(&mut self, id: OrderId, p: Px, q: Qty, ts: Ts) -> Option<OrderId> {
        let old = self.orders.remove(&id)?;
        Some(self.place(old.side, p, q, ts))
    }

    pub fn get(&self, id: OrderId) -> Option<&OwnOrder> {
        self.orders.get(&id)
    }

    pub fn iter(&self) -> impl Iterator<Item = &OwnOrder> {
        self.orders.values()
    }

    pub fn iter_side(&self, s: Side) -> impl Iterator<Item = &OwnOrder> {
        self.orders.values().filter(move |o| o.side == s)
    }

    pub fn reduce_qty(&mut self, id: OrderId, by: Qty) -> Option<&OwnOrder> {
        let o = self.orders.get_mut(&id)?;
        if by >= o.qty {
            self.orders.remove(&id);
            return None;
        }
        o.qty -= by;
        self.orders.get(&id)
    }

    pub fn len(&self) -> usize {
        self.orders.len()
    }

    pub fn is_empty(&self) -> bool {
        self.orders.is_empty()
    }
}

impl Default for OwnOrders {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn px(s: &str) -> Px {
        Px(s.parse().unwrap())
    }

    #[test]
    fn place_cancel() {
        let mut o = OwnOrders::new();
        let id = o.place(Side::Bid, px("100"), 10, 1);
        assert_eq!(o.cancel(id).unwrap().qty, 10);
        assert!(o.is_empty());
    }
    #[test]
    fn cancel_all_side_filters() {
        let mut o = OwnOrders::new();
        o.place(Side::Bid, px("100"), 1, 1);
        o.place(Side::Ask, px("102"), 1, 1);
        o.place(Side::Bid, px("99"), 1, 1);
        assert_eq!(o.cancel_all_side(Side::Bid).len(), 2);
        assert_eq!(o.len(), 1);
    }
    #[test]
    fn replace_new_id() {
        let mut o = OwnOrders::new();
        let id1 = o.place(Side::Bid, px("100"), 5, 1);
        let id2 = o.replace(id1, px("99"), 5, 2).unwrap();
        assert_ne!(id1, id2);
        assert!(o.get(id1).is_none());
    }
    #[test]
    fn reduce_removes_when_filled() {
        let mut o = OwnOrders::new();
        let id = o.place(Side::Bid, px("100"), 10, 1);
        assert!(o.reduce_qty(id, 4).is_some());
        assert_eq!(o.get(id).unwrap().qty, 6);
        assert!(o.reduce_qty(id, 6).is_none());
        assert!(o.get(id).is_none());
    }
    #[test]
    fn ids_monotonic() {
        let mut o = OwnOrders::new();
        let mut last = 0;
        for _ in 0..50 {
            let id = o.place(Side::Bid, px("100"), 1, 1);
            assert!(id > last);
            last = id;
        }
    }
}
