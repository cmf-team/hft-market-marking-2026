use crate::engine::Run;
use crate::fill_engine::Fill;
use crate::metrics::EquityPoint;
use serde::Serialize;
use std::fs::File;
use std::io::{BufWriter, Write};
use std::path::Path;

#[derive(Serialize)]
pub struct SummaryJson {
    pub strategy: String,
    pub session_start_us: u64,
    pub session_end_us: u64,
    pub final_equity: Option<String>,
    pub final_position: i64,
    pub max_abs_position: i64,
    pub turnover_notional: String,
    pub trade_count: u64,
    pub buys: u64,
    pub sells: u64,
}

pub fn write_fills_csv(path: &Path, fills: &[Fill]) -> std::io::Result<()> {
    let mut w = csv::Writer::from_writer(BufWriter::new(File::create(path)?));
    w.write_record(["ts_us", "order_id", "side", "price", "qty"])?;
    for f in fills {
        let side = match f.side {
            crate::types::Side::Bid => "bid",
            crate::types::Side::Ask => "ask",
        };
        w.write_record(&[
            f.ts_us.to_string(),
            f.order_id.to_string(),
            side.to_string(),
            f.price.0.to_string(),
            f.qty.to_string(),
        ])?;
    }
    w.flush()?;
    Ok(())
}

pub fn write_equity_curve_csv(path: &Path, points: &[EquityPoint]) -> std::io::Result<()> {
    let mut w = csv::Writer::from_writer(BufWriter::new(File::create(path)?));
    w.write_record(["ts_us", "mid", "equity", "position"])?;
    for p in points {
        w.write_record(&[
            p.ts_us.to_string(),
            p.mid.0.to_string(),
            p.equity.to_string(),
            p.position.to_string(),
        ])?;
    }
    w.flush()?;
    Ok(())
}

pub fn write_summary_json(path: &Path, strategy: &str, run: &Run) -> std::io::Result<()> {
    let s = run.metrics.summary(run.last_mid);
    let summary = SummaryJson {
        strategy: strategy.to_string(),
        session_start_us: run.session_start_us,
        session_end_us: run.session_end_us,
        final_equity: s.final_equity.map(|d| d.to_string()),
        final_position: s.final_position,
        max_abs_position: s.max_abs_position,
        turnover_notional: s.turnover_notional.to_string(),
        trade_count: s.trade_count,
        buys: s.buys,
        sells: s.sells,
    };
    let mut f = BufWriter::new(File::create(path)?);
    f.write_all(serde_json::to_string_pretty(&summary).unwrap().as_bytes())?;
    f.write_all(b"\n")?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::metrics::EquityPoint;
    use crate::types::{Px, Side};

    #[test]
    fn fills_csv_round_trip() {
        let dir = tempfile::tempdir().unwrap();
        let p = dir.path().join("fills.csv");
        let fills = vec![Fill {
            ts_us: 1,
            order_id: 1,
            side: Side::Bid,
            price: Px("100".parse().unwrap()),
            qty: 5,
        }];
        write_fills_csv(&p, &fills).unwrap();
        let s = std::fs::read_to_string(&p).unwrap();
        assert!(s.contains("ts_us"));
        assert!(s.contains("bid"));
        assert!(s.contains("100"));
    }

    #[test]
    fn equity_curve_csv_writes_rows() {
        let dir = tempfile::tempdir().unwrap();
        let p = dir.path().join("eq.csv");
        let points = vec![EquityPoint {
            ts_us: 1,
            mid: Px("100".parse().unwrap()),
            equity: "0".parse().unwrap(),
            position: 0,
        }];
        write_equity_curve_csv(&p, &points).unwrap();
        let s = std::fs::read_to_string(&p).unwrap();
        assert!(s.lines().count() >= 2);
    }
}
