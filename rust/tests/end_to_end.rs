//! End-to-end test: drive a complete backtest through `Config` → engine →
//! report writers, and verify all output artifacts (fills.csv, equity.csv,
//! summary.json) are produced with the expected schema.

use hft_mm_backtester::config::Config;
use hft_mm_backtester::engine::{self, EngineCfg};
use hft_mm_backtester::parser::{lob, LobReader, MergedEvents, TradeReader};
use hft_mm_backtester::report::{write_equity_curve_csv, write_fills_csv, write_summary_json};
use hft_mm_backtester::strategy::symmetric::SymmetricMM;
use std::fs::File;
use std::io::{BufReader, Write};
use std::path::Path;

fn write_lob_fixture(path: &Path) {
    let header = lob::expected_header();
    // 5 snapshots, with row 4 (ts=4000) crossing the book to drive a fill.
    let rows: [(u64, &str, &str); 5] = [
        (1000, "102.0", "100.0"),
        (2000, "102.0", "100.0"),
        (3000, "102.0", "100.0"),
        (4000, "99.5", "100.0"),
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
    File::create(path)
        .unwrap()
        .write_all(out.as_bytes())
        .unwrap();
}

fn write_trades_fixture(path: &Path) {
    let body = ",local_timestamp,side,price,amount\n\
                0,1500,sell,99.5,1\n\
                1,2500,sell,99.5,2\n\
                2,3500,sell,99.5,1\n";
    File::create(path)
        .unwrap()
        .write_all(body.as_bytes())
        .unwrap();
}

#[test]
fn full_backtest_produces_all_artifacts() {
    let dir = tempfile::tempdir().unwrap();
    let lob_path = dir.path().join("lob.csv");
    let trades_path = dir.path().join("trades.csv");
    let out_dir = dir.path().join("out");
    std::fs::create_dir_all(&out_dir).unwrap();

    write_lob_fixture(&lob_path);
    write_trades_fixture(&trades_path);

    // Build a config TOML and load it (mirrors what the `backtest` binary does).
    let toml = format!(
        r#"
[data]
lob_csv = "{lob}"
trades_csv = "{trades}"

[engine]
tick_ms = 1
allow_partial_fills = true

[strategy]
kind = "symmetric"
[strategy.symmetric]
half_spread = "0.5"
quote_qty = 1
max_abs_inventory = 100

[output]
dir = "{out}"
equity_curve = true
fills = true
"#,
        lob = lob_path.to_string_lossy(),
        trades = trades_path.to_string_lossy(),
        out = out_dir.to_string_lossy(),
    );
    let cfg_path = dir.path().join("cfg.toml");
    File::create(&cfg_path)
        .unwrap()
        .write_all(toml.as_bytes())
        .unwrap();
    let cfg = Config::from_path(&cfg_path).expect("config loads");

    // Run the engine end-to-end.
    let lob_reader =
        LobReader::new(BufReader::new(File::open(&cfg.data.lob_csv).unwrap())).unwrap();
    let trades_reader =
        TradeReader::new(BufReader::new(File::open(&cfg.data.trades_csv).unwrap())).unwrap();
    let merged = MergedEvents::new(lob_reader, trades_reader);

    let s = cfg.strategy.symmetric.as_ref().unwrap();
    let mut strat = SymmetricMM::new(
        s.half_spread.parse().unwrap(),
        s.quote_qty,
        s.max_abs_inventory,
    );
    let ecfg = EngineCfg {
        tick_ms: cfg.engine.tick_ms,
        allow_partial_fills: cfg.engine.allow_partial_fills,
        start_us: cfg.engine.start_us,
        end_us: cfg.engine.end_us,
    };
    let run = engine::run(merged, &mut strat, &ecfg).expect("engine run ok");

    // Write reports (same as the binary does).
    let fills_csv = out_dir.join("fills.csv");
    let equity_csv = out_dir.join("equity.csv");
    let summary_json = out_dir.join("summary.json");
    write_fills_csv(&fills_csv, &run.fills).unwrap();
    write_equity_curve_csv(&equity_csv, &run.metrics.equity_curve).unwrap();
    write_summary_json(&summary_json, "symmetric", &run).unwrap();

    // All three artifacts exist.
    assert!(fills_csv.exists(), "fills.csv should exist");
    assert!(equity_csv.exists(), "equity.csv should exist");
    assert!(summary_json.exists(), "summary.json should exist");

    // Fills CSV: header + ≥1 fill row.
    let fills_body = std::fs::read_to_string(&fills_csv).unwrap();
    let fills_lines: Vec<&str> = fills_body.lines().collect();
    assert_eq!(
        fills_lines[0], "ts_us,order_id,side,price,qty",
        "fills header"
    );
    assert!(fills_lines.len() >= 2, "expected at least one fill row");

    // Equity CSV: header + ≥1 sample.
    let eq_body = std::fs::read_to_string(&equity_csv).unwrap();
    let eq_lines: Vec<&str> = eq_body.lines().collect();
    assert_eq!(eq_lines[0], "ts_us,mid,equity,position", "equity header");
    assert!(eq_lines.len() >= 2, "expected at least one equity sample");

    // Summary JSON: parses, names the strategy, records the trade.
    let summary: serde_json::Value =
        serde_json::from_str(&std::fs::read_to_string(&summary_json).unwrap()).unwrap();
    assert_eq!(summary["strategy"], "symmetric");
    assert!(summary["trade_count"].as_u64().unwrap() >= 1);
    assert!(
        summary["session_start_us"].as_u64().unwrap()
            <= summary["session_end_us"].as_u64().unwrap()
    );
    assert!(summary["final_position"].as_i64().is_some());
}
