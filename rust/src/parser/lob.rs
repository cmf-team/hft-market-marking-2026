use rust_decimal::Decimal;

use super::ParseError;
use crate::types::{Px, Qty, Ts};

/// Number of price levels per side in the dataset
pub const LEVELS: usize = 25;

/// One full L2 snapshot at a given timestamp.
/// `bids` and `asks` are sorted best-first and contain at most `LEVELS` entries.
/// Empty levels in the CSV (zero price/zero size) are skipped, so the vectors may be shorter
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct BookSnapshot {
    pub ts_us: Ts,
    pub seq: u64,
    pub bids: Vec<(Px, Qty)>,
    pub asks: Vec<(Px, Qty)>,
}

impl BookSnapshot {
    pub fn empty(ts_us: Ts) -> Self {
        Self {
            ts_us,
            seq: 0,
            bids: Vec::new(),
            asks: Vec::new(),
        }
    }
}

/// Expected CSV header for `lob.csv`
pub fn expected_header() -> String {
    let mut s = String::from(",local_timestamp");
    for i in 0..LEVELS {
        s.push_str(&format!(
            ",asks[{i}].price,asks[{i}].amount,bids[{i}].price,bids[{i}].amount"
        ));
    }
    s
}

/// Parse a single row of `lob.csv` (already split into fields by the csv crate).
/// `fields[0]` = row index, `fields[1]` = timestamp, then 100 alternating fields.
pub fn parse_row(fields: &csv::StringRecord) -> Result<BookSnapshot, ParseError> {
    let total = 2 + 4 * LEVELS;
    if fields.len() != total {
        return Err(ParseError::BadField {
            field: "row_length",
            value: fields.len().to_string(),
        });
    }

    let ts_us: Ts = fields
        .get(1)
        .ok_or(ParseError::MissingField("local_timestamp"))?
        .parse()
        .map_err(|_| ParseError::BadField {
            field: "local_timestamp",
            value: fields.get(1).unwrap_or("").to_string(),
        })?;

    let mut snap = BookSnapshot::empty(ts_us);
    for i in 0..LEVELS {
        let base = 2 + 4 * i;
        let ask = parse_pair(fields, base, base + 1, "asks")?;
        let bid = parse_pair(fields, base + 2, base + 3, "bids")?;
        // Skip empty levels (zero qty, zero price): they pad short books in the CSV.
        if ask.1 > 0 && !ask.0 .0.is_zero() {
            snap.asks.push(ask);
        }
        if bid.1 > 0 && !bid.0 .0.is_zero() {
            snap.bids.push(bid);
        }
    }

    Ok(snap)
}

fn parse_pair(
    fields: &csv::StringRecord,
    px_idx: usize,
    qty_idx: usize,
    name: &'static str,
) -> Result<(Px, Qty), ParseError> {
    let px_str = fields.get(px_idx).ok_or(ParseError::MissingField(name))?;
    let qty_str = fields.get(qty_idx).ok_or(ParseError::MissingField(name))?;
    let px: Decimal = px_str.parse().map_err(|_| ParseError::BadField {
        field: name,
        value: px_str.to_string(),
    })?;
    let qty = parse_qty(qty_str)?;
    Ok((Px(px), qty))
}

/// Parse `Qty` from a string that may have a `.0` float suffix.
pub fn parse_qty(s: &str) -> Result<Qty, ParseError> {
    let f: f64 = s.parse().map_err(|_| ParseError::BadField {
        field: "qty",
        value: s.to_string(),
    })?;
    if f < 0.0 || f.fract() != 0.0 || !f.is_finite() {
        return Err(ParseError::NonIntegerQty(s.to_string()));
    }
    Ok(f as Qty)
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
    fn header_string_has_correct_shape() {
        let h = expected_header();
        assert!(h.starts_with(
            ",local_timestamp,asks[0].price,asks[0].amount,bids[0].price,bids[0].amount"
        ));
        assert!(h.ends_with(",asks[24].price,asks[24].amount,bids[24].price,bids[24].amount"));
    }

    #[test]
    fn parse_qty_accepts_integer_suffix() {
        assert_eq!(parse_qty("121492.0").unwrap(), 121492);
        assert_eq!(parse_qty("100").unwrap(), 100);
    }

    #[test]
    fn parse_qty_rejects_fractional() {
        assert!(matches!(
            parse_qty("12.5"),
            Err(ParseError::NonIntegerQty(_))
        ));
    }

    #[test]
    fn parse_qty_rejects_negative() {
        assert!(matches!(
            parse_qty("-1.0"),
            Err(ParseError::NonIntegerQty(_))
        ));
    }

    #[test]
    fn parse_real_row() {
        // Real first data row from lob.csv, truncated to 25 levels.
        let mut row = String::from("0,1722470402038431");
        for i in 0..LEVELS {
            row.push_str(&format!(",0.0110{:03},10.0,0.0109{:03},20.0", i, i));
        }
        let rec = record(&row);
        let snap = parse_row(&rec).unwrap();
        assert_eq!(snap.ts_us, 1722470402038431);
        assert_eq!(snap.asks.len(), LEVELS);
        assert_eq!(snap.bids.len(), LEVELS);
        assert_eq!(snap.asks[0], (Px(dec!(0.0110000)), 10));
        assert_eq!(snap.bids[0], (Px(dec!(0.0109000)), 20));
        assert_eq!(snap.asks[24].1, 10);
    }

    #[test]
    fn parse_rejects_short_row() {
        let rec = record("0,1722470402038431,1.0,1.0");
        assert!(matches!(
            parse_row(&rec),
            Err(ParseError::BadField {
                field: "row_length",
                ..
            })
        ));
    }

    #[test]
    fn parse_rejects_bad_timestamp() {
        let mut row = String::from("0,not_a_ts");
        for _ in 0..LEVELS {
            row.push_str(",1.0,1.0,1.0,1.0");
        }
        let rec = record(&row);
        assert!(matches!(
            parse_row(&rec),
            Err(ParseError::BadField {
                field: "local_timestamp",
                ..
            })
        ));
    }

    #[test]
    fn parse_rejects_bad_price() {
        let mut row = String::from("0,1722470402038431");
        row.push_str(",not_a_price,1.0,1.0,1.0");
        for _ in 1..LEVELS {
            row.push_str(",1.0,1.0,1.0,1.0");
        }
        let rec = record(&row);
        assert!(matches!(
            parse_row(&rec),
            Err(ParseError::BadField { field: "asks", .. })
        ));
    }
}
