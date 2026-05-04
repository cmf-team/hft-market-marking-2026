//! Microbenchmarks for the streaming CSV parsers.
//!
//! Generates a synthetic in-memory LOB feed (1k rows) and a trades feed (1k
//! rows), then measures throughput of `LobReader`, `TradeReader`, and the
//! merged 2-way iterator.

use criterion::{criterion_group, criterion_main, BenchmarkId, Criterion, Throughput};
use hft_mm_backtester::parser::{lob, LobReader, MergedEvents, TradeReader};
use std::io::Cursor;

fn synth_lob(n: usize) -> String {
    let mut s = String::new();
    s.push_str(&lob::expected_header());
    s.push('\n');
    for i in 0..n {
        s.push_str(&format!("{},{}", i, 1_000_000_000u64 + i as u64));
        for lvl in 0..lob::LEVELS {
            // Always populate level 0; pad zero on deeper levels.
            if lvl == 0 {
                s.push_str(",100.5,5,99.5,5");
            } else {
                s.push_str(",0,0,0,0");
            }
        }
        s.push('\n');
    }
    s
}

fn synth_trades(n: usize) -> String {
    let mut s = String::from(",local_timestamp,side,price,amount\n");
    for i in 0..n {
        let side = if i % 2 == 0 { "buy" } else { "sell" };
        s.push_str(&format!(
            "{},{},{},100.0,1\n",
            i,
            1_000_000_000u64 + i as u64,
            side
        ));
    }
    s
}

fn bench_lob(c: &mut Criterion) {
    let mut g = c.benchmark_group("parse_lob");
    for n in &[100usize, 1_000] {
        let buf = synth_lob(*n);
        g.throughput(Throughput::Elements(*n as u64));
        g.bench_with_input(BenchmarkId::from_parameter(n), n, |b, _| {
            b.iter(|| {
                let r = LobReader::new(Cursor::new(buf.as_bytes())).unwrap();
                let count = r.filter_map(Result::ok).count();
                criterion::black_box(count);
            });
        });
    }
    g.finish();
}

fn bench_trades(c: &mut Criterion) {
    let mut g = c.benchmark_group("parse_trades");
    for n in &[100usize, 1_000] {
        let buf = synth_trades(*n);
        g.throughput(Throughput::Elements(*n as u64));
        g.bench_with_input(BenchmarkId::from_parameter(n), n, |b, _| {
            b.iter(|| {
                let r = TradeReader::new(Cursor::new(buf.as_bytes())).unwrap();
                let count = r.filter_map(Result::ok).count();
                criterion::black_box(count);
            });
        });
    }
    g.finish();
}

fn bench_merged(c: &mut Criterion) {
    let mut g = c.benchmark_group("parse_merged");
    let lob_buf = synth_lob(1_000);
    let trd_buf = synth_trades(1_000);
    g.throughput(Throughput::Elements(2_000));
    g.bench_function("1k_lob+1k_trades", |b| {
        b.iter(|| {
            let lob = LobReader::new(Cursor::new(lob_buf.as_bytes())).unwrap();
            let trd = TradeReader::new(Cursor::new(trd_buf.as_bytes())).unwrap();
            let n = MergedEvents::new(lob, trd).filter_map(Result::ok).count();
            criterion::black_box(n);
        });
    });
    g.finish();
}

criterion_group!(benches, bench_lob, bench_trades, bench_merged);
criterion_main!(benches);
