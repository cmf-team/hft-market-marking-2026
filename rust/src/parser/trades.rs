use rust_decimal::Decimal;

use super::{lob::parse_qty, ParseError};
use crate::types::{Px, Qty, Side, Ts};

/// One trade print: aggressor side at price for quantity.
/// `side` is the aggressor side (the side that initiated the trade)
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TradePrint {
    pub ts_us: Ts,
    pub side: Side,
    pub price: Px,
    pub size: Qty,
}

pub fn expected_header() -> &'static str {
    ",local_timestamp,side,price,amount"
}

pub fn parse_row(fields: &csv::StringRecord) -> Result<TradePrint, ParseError> {
    if fields.len() != 5 {
        return Err(ParseError::BadField {
            field: "row_length",
            value: fields.len().to_string(),
        });
    }
    let ts_us: Ts = fields
        .get(1)
        .unwrap()
        .parse()
        .map_err(|_| ParseError::BadField {
            field: "local_timestamp",
            value: fields.get(1).unwrap().to_string(),
        })?;
    let side_str = fields.get(2).unwrap();
    let side = match side_str {
        "buy" => Side::Bid,  // aggressor is buyer → consumes asks
        "sell" => Side::Ask, // aggressor is seller → consumes bids
        _ => {
            return Err(ParseError::BadField {
                field: "side",
                value: side_str.to_string(),
            })
        }
    };
    let px_str = fields.get(3).unwrap();
    let price: Decimal = px_str.parse().map_err(|_| ParseError::BadField {
        field: "price",
        value: px_str.to_string(),
    })?;
    let size = parse_qty(fields.get(4).unwrap())?;
    Ok(TradePrint {
        ts_us,
        side,
        price: Px(price),
        size,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use rust_decimal_macros::dec;

    fn record(s: &str) -> csv::StringRecord {
        let mut r = csv::ReaderBuilder::new()
            .has_headers(false)
            .from_reader(s.as_bytes());
        r.records().next().unwrap().unwrap()
    }

    #[test]
    fn parse_buy_row() {
        let r = record("0,1722470400014926,buy,0.0110435,734");
        let t = parse_row(&r).unwrap();
        assert_eq!(t.ts_us, 1722470400014926);
        assert_eq!(t.side, Side::Bid);
        assert_eq!(t.price, Px(dec!(0.0110435)));
        assert_eq!(t.size, 734);
    }

    #[test]
    fn parse_sell_row() {
        let r = record("0,1722470400014926,sell,0.0110435,1633");
        let t = parse_row(&r).unwrap();
        assert_eq!(t.side, Side::Ask);
    }

    #[test]
    fn parse_rejects_bad_side() {
        let r = record("0,1722470400014926,xxx,0.0110435,1");
        assert!(matches!(
            parse_row(&r),
            Err(ParseError::BadField { field: "side", .. })
        ));
    }

    #[test]
    fn parse_rejects_short_row() {
        let r = record("0,1");
        assert!(matches!(
            parse_row(&r),
            Err(ParseError::BadField {
                field: "row_length",
                ..
            })
        ));
    }

    #[test]
    fn parse_rejects_bad_price() {
        let r = record("0,1722470400014926,buy,abc,1");
        assert!(matches!(
            parse_row(&r),
            Err(ParseError::BadField { field: "price", .. })
        ));
    }

    #[test]
    fn parse_rejects_bad_qty() {
        let r = record("0,1722470400014926,buy,0.01,1.5");
        assert!(matches!(parse_row(&r), Err(ParseError::NonIntegerQty(_))));
    }
}
