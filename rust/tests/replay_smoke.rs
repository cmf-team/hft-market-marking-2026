//! End-to-end smoke test: stream a tiny LOB + trades fixture through the
//! full engine using the SymmetricMM baseline and confirm the run produces
//! at least one fill on a manufactured cross.

use hft_mm_backtester::engine::{self, EngineCfg};
use hft_mm_backtester::parser::{lob, LobReader, MergedEvents, TradeReader};
use hft_mm_backtester::strategy::symmetric::SymmetricMM;
use std::fs::File;
use std::io::{BufReader, Write};
use std::path::PathBuf;

/// Programmatically generate `tests/fixtures/lob_small.csv` so it always has
/// the right number of columns. 5 snapshots: rows 1-3 quote 100/102, row 4
/// flips the book to 100/99.5 (crossed) to force a fill against our resting
/// quotes, row 5 returns to 100/102.
fn ensure_lob_fixture() -> PathBuf {
    let path = PathBuf::from("tests/fixtures/lob_small.csv");
    let header = lob::expected_header();
    // (best_ask_px, best_bid_px) at each ts; deeper levels are zero-padded.
    let rows: [(u64, &str, &str); 5] = [
        (1000, "102.0", "100.0"),
        (2000, "102.0", "100.0"),
        (3000, "102.0", "100.0"),
        (4000, "99.5", "100.0"), // crossed — drives our bid into a fill
        (5000, "102.0", "100.0"),
    ];
    let mut out = String::new();
    out.push_str(&header);
    out.push('\n');
    for (i, (ts, ask, bid)) in rows.iter().enumerate() {
        let mut line = format!("{i},{ts}");
        for lvl in 0..lob::LEVELS {
            if lvl == 0 {
                line.push_str(&format!(",{ask},5.0,{bid},5.0"));
            } else {
                line.push_str(",0.0,0.0,0.0,0.0");
            }
        }
        out.push_str(&line);
        out.push('\n');
    }
    let mut f = File::create(&path).expect("write lob fixture");
    f.write_all(out.as_bytes()).expect("write lob fixture");
    path
}

#[test]
fn smoke_replay_with_symmetric_yields_fills() {
    let lob_path = ensure_lob_fixture();
    let trades_path = PathBuf::from("tests/fixtures/trades_small.csv");

    let lob = LobReader::new(BufReader::new(File::open(&lob_path).unwrap())).unwrap();
    let tr = TradeReader::new(BufReader::new(File::open(&trades_path).unwrap())).unwrap();
    let merged = MergedEvents::new(lob, tr);

    // half_spread=0.5 → quotes 99.5/101.5 around mid 100; quote_qty=1.
    let mut strat = SymmetricMM::new("0.5".parse().unwrap(), 1, 100);
    let cfg = EngineCfg {
        tick_ms: 1, // 1 ms = 1000 µs, so a tick fires on every fixture step
        allow_partial_fills: true,
        start_us: None,
        end_us: None,
    };

    let run = engine::run(merged, &mut strat, &cfg).expect("engine run ok");

    assert!(
        !run.fills.is_empty(),
        "expected at least one fill from the manufactured cross"
    );
    assert!(
        run.metrics.trade_count > 0,
        "metrics should record the fill(s)"
    );
    assert!(
        !run.metrics.equity_curve.is_empty(),
        "equity curve should have at least one sample"
    );
}
