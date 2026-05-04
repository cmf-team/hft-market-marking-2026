pub mod lob;
pub mod trades;

use std::io::Read;
use thiserror::Error;

use crate::types::Ts;
use lob::BookSnapshot;
use trades::TradePrint;

#[derive(Debug, Error)]
pub enum ParseError {
    #[error("io error: {0}")]
    Io(#[from] std::io::Error),
    #[error("csv error: {0}")]
    Csv(#[from] csv::Error),
    #[error("invalid header: expected {expected}, got {got}")]
    BadHeader { expected: String, got: String },
    #[error("missing field '{0}'")]
    MissingField(&'static str),
    #[error("invalid {field}: '{value}'")]
    BadField { field: &'static str, value: String },
    #[error("non-integer quantity: '{0}'")]
    NonIntegerQty(String),
}

/// Time-ordered tagged event yielded by `MergedEvents`.
#[derive(Clone, Debug)]
pub enum MdEvent {
    BookUpdate(BookSnapshot),
    TradePrint(TradePrint),
}

impl MdEvent {
    pub fn ts_us(&self) -> Ts {
        match self {
            MdEvent::BookUpdate(s) => s.ts_us,
            MdEvent::TradePrint(t) => t.ts_us,
        }
    }
}

/// Iterator over `BookSnapshot`s from a `lob.csv` reader.
pub struct LobReader<R: Read> {
    rdr: csv::Reader<R>,
    rec: csv::StringRecord,
}

impl<R: Read> LobReader<R> {
    pub fn new(r: R) -> Result<Self, ParseError> {
        let mut rdr = csv::ReaderBuilder::new().has_headers(true).from_reader(r);
        
        validate_header(rdr.headers()?, &lob::expected_header())?;
        
        Ok(Self {
            rdr,
            rec: csv::StringRecord::new(),
        })
    }
}

impl<R: Read> Iterator for LobReader<R> {
    type Item = Result<BookSnapshot, ParseError>;

    fn next(&mut self) -> Option<Self::Item> {
        match self.rdr.read_record(&mut self.rec) {
            Ok(true) => Some(lob::parse_row(&self.rec)),
            Ok(false) => None,
            Err(e) => Some(Err(ParseError::Csv(e))),
        }
    }
}

pub struct TradeReader<R: Read> {
    rdr: csv::Reader<R>,
    rec: csv::StringRecord,
}

impl<R: Read> TradeReader<R> {
    pub fn new(r: R) -> Result<Self, ParseError> {
        let mut rdr = csv::ReaderBuilder::new().has_headers(true).from_reader(r);
        
        validate_header(rdr.headers()?, trades::expected_header())?;
        
        Ok(Self {
            rdr,
            rec: csv::StringRecord::new(),
        })
    }
}

fn validate_header(header: &csv::StringRecord, expected: &str) -> Result<(), ParseError> {
    let expected_fields: Vec<&str> = expected.split(',').collect();
    
    if header.iter().eq(expected_fields.iter().copied()) {
        Ok(())
    } else {
        Err(ParseError::BadHeader {
            expected: expected.to_string(),
            got: header.iter().collect::<Vec<_>>().join(","),
        })
    }
}

impl<R: Read> Iterator for TradeReader<R> {
    type Item = Result<TradePrint, ParseError>;

    fn next(&mut self) -> Option<Self::Item> {
        match self.rdr.read_record(&mut self.rec) {
            Ok(true) => Some(trades::parse_row(&self.rec)),
            Ok(false) => None,
            Err(e) => Some(Err(ParseError::Csv(e))),
        }
    }
}

pub struct MergedEvents<L, T>
where
    L: Iterator<Item = Result<BookSnapshot, ParseError>>,
    T: Iterator<Item = Result<TradePrint, ParseError>>,
{
    lob: std::iter::Peekable<L>,
    trd: std::iter::Peekable<T>,
}

impl<L, T> MergedEvents<L, T>
where
    L: Iterator<Item = Result<BookSnapshot, ParseError>>,
    T: Iterator<Item = Result<TradePrint, ParseError>>,
{
    pub fn new(lob: L, trd: T) -> Self {
        Self {
            lob: lob.peekable(),
            trd: trd.peekable(),
        }
    }
}

impl<L, T> Iterator for MergedEvents<L, T>
where
    L: Iterator<Item = Result<BookSnapshot, ParseError>>,
    T: Iterator<Item = Result<TradePrint, ParseError>>,
{
    type Item = Result<MdEvent, ParseError>;
    fn next(&mut self) -> Option<Self::Item> {
        let lob_ts = match self.lob.peek() {
            Some(Ok(s)) => Some(s.ts_us),
            Some(Err(_)) => return self.lob.next().map(|r| r.map(MdEvent::BookUpdate)),
            None => None,
        };

        let trd_ts = match self.trd.peek() {
            Some(Ok(t)) => Some(t.ts_us),
            Some(Err(_)) => return self.trd.next().map(|r| r.map(MdEvent::TradePrint)),
            None => None,
        };
        
        match (lob_ts, trd_ts) {
            (None, None) => None,
            (Some(_), None) => self.lob.next().map(|r| r.map(MdEvent::BookUpdate)),
            (None, Some(_)) => self.trd.next().map(|r| r.map(MdEvent::TradePrint)),
            (Some(a), Some(b)) => {
                if a <= b {
                    self.lob.next().map(|r| r.map(MdEvent::BookUpdate))
                } else {
                    self.trd.next().map(|r| r.map(MdEvent::TradePrint))
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    fn lob_iter() -> Vec<Result<BookSnapshot, ParseError>> {
        let p = "tests/fixtures/tiny_lob.csv";
        let f = std::fs::File::open(p).expect("missing fixture");
        LobReader::new(f).unwrap().collect()
    }
    fn trd_iter() -> Vec<Result<TradePrint, ParseError>> {
        let p = "tests/fixtures/tiny_trades.csv";
        let f = std::fs::File::open(p).expect("missing fixture");
        TradeReader::new(f).unwrap().collect()
    }

    #[test]
    fn lob_reader_yields_three_rows() {
        let v: Vec<_> = lob_iter().into_iter().collect::<Result<_, _>>().unwrap();
        assert_eq!(v.len(), 3);
        assert_eq!(v[0].ts_us, 1000);
        assert_eq!(v[2].ts_us, 3000);
    }

    #[test]
    fn trade_reader_yields_three_rows() {
        let v: Vec<_> = trd_iter().into_iter().collect::<Result<_, _>>().unwrap();
        assert_eq!(v.len(), 3);
    }

    #[test]
    fn lob_reader_from_in_memory_buffer() {
        let header = lob::expected_header();
        let mut row = String::from("0,42");
        for _ in 0..lob::LEVELS {
            row.push_str(",1.0,1.0,1.0,1.0");
        }
        let buf = format!("{}\n{}\n", header, row);
        let v: Vec<_> = LobReader::new(Cursor::new(buf))
            .unwrap()
            .collect::<Result<_, _>>()
            .unwrap();
        assert_eq!(v[0].ts_us, 42);
    }

    #[test]
    fn lob_reader_rejects_wrong_header() {
        let mut row = String::from("0,42");
        for _ in 0..lob::LEVELS {
            row.push_str(",1.0,1.0,1.0,1.0");
        }
        let buf = format!("wrong,header\n{}\n", row);

        match LobReader::new(Cursor::new(buf)) {
            Err(ParseError::BadHeader { .. }) => {}
            Err(e) => panic!("unexpected error: {e}"),
            Ok(_) => panic!("reader accepted a wrong header"),
        }
    }

    #[test]
    fn trade_reader_rejects_wrong_header() {
        let buf = "wrong,header,side,price,amount\n0,42,buy,1.0,1.0\n";

        match TradeReader::new(Cursor::new(buf)) {
            Err(ParseError::BadHeader { .. }) => {}
            Err(e) => panic!("unexpected error: {e}"),
            Ok(_) => panic!("reader accepted a wrong header"),
        }
    }
}
