use rust_decimal::Decimal;
use serde::{Deserialize, Serialize};
use std::fmt;

pub type Ts = u64;
pub type OrderId = u64;
pub type Qty = u64;

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize, Deserialize)]
pub struct Px(pub Decimal);

impl Px {
    pub const ZERO: Px = Px(Decimal::ZERO);

    pub fn new(d: Decimal) -> Self {
        Self(d)
    }

    pub fn inner(self) -> Decimal {
        self.0
    }
}

impl fmt::Display for Px {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0.normalize())
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum Side {
    Bid,
    Ask,
}

impl Side {
    pub fn flip(self) -> Side {
        match self {
            Side::Bid => Side::Ask,
            Side::Ask => Side::Bid,
        }
    }
}

impl fmt::Display for Side {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let s = match self {
            Side::Bid => "Bid",
            Side::Ask => "Ask",
        };
        write!(f, "{s}")
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rust_decimal_macros::dec;

    #[test]
    fn side_flip_round_trip() {
        assert_eq!(Side::Bid.flip(), Side::Ask);
        assert_eq!(Side::Ask.flip(), Side::Bid);
        assert_eq!(Side::Bid.flip().flip(), Side::Bid);
    }

    #[test]
    fn px_ordering() {
        assert!(Px(dec!(0.0110436)) > Px(dec!(0.0110435)));
        assert_eq!(Px::ZERO, Px(Decimal::ZERO));
    }

    #[test]
    fn px_display_normalized() {
        assert_eq!(Px(dec!(0.011000)).to_string(), "0.011");
    }

    #[test]
    fn side_display() {
        assert_eq!(Side::Bid.to_string(), "Bid");
        assert_eq!(Side::Ask.to_string(), "Ask");
    }
}
