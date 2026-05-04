//! Microbenchmarks for the full replay engine.
//!
//! Builds a synthetic stream of `BookSnapshot`s in memory and runs both the
//! Symmetric and Avellaneda–Stoikov strategies through the engine. Useful as
//! a regression guard against accidentally-quadratic changes in the inner
//! loop (fill matching, equity sampling, tick scheduling).

use criterion::{criterion_group, criterion_main, Criterion, Throughput};
use hft_mm_backtester::engine::{self, EngineCfg};
use hft_mm_backtester::parser::lob::BookSnapshot;
use hft_mm_backtester::parser::{MdEvent, ParseError};
use hft_mm_backtester::strategy::avellaneda_stoikov::{AsParams, AvellanedaStoikov};
use hft_mm_backtester::strategy::symmetric::SymmetricMM;
use hft_mm_backtester::types::Px;

fn synth_events(n: usize) -> Vec<Result<MdEvent, ParseError>> {
    let bid: Px = Px("100".parse().unwrap());
    let ask: Px = Px("101".parse().unwrap());
    (0..n)
        .map(|i| {
            Ok(MdEvent::BookUpdate(BookSnapshot {
                ts_us: 1_000 + 1_000 * i as u64,
                seq: i as u64,
                bids: vec![(bid, 5)],
                asks: vec![(ask, 5)],
            }))
        })
        .collect()
}

fn cfg() -> EngineCfg {
    EngineCfg {
        tick_ms: 1,
        allow_partial_fills: true,
        start_us: None,
        end_us: None,
    }
}

fn bench_replay_symmetric(c: &mut Criterion) {
    let mut g = c.benchmark_group("replay_symmetric");
    let n = 10_000;
    g.throughput(Throughput::Elements(n as u64));
    g.bench_function("10k_events", |b| {
        b.iter(|| {
            let evs = synth_events(n);
            let mut s = SymmetricMM::new("0.5".parse().unwrap(), 1, 100);
            let r = engine::run(evs.into_iter(), &mut s, &cfg()).unwrap();
            criterion::black_box(r.fills.len());
        });
    });
    g.finish();
}

fn bench_replay_as(c: &mut Criterion) {
    let mut g = c.benchmark_group("replay_as");
    let n = 10_000;
    g.throughput(Throughput::Elements(n as u64));
    g.bench_function("10k_events", |b| {
        b.iter(|| {
            let evs = synth_events(n);
            let mut s = AvellanedaStoikov::new(AsParams {
                sigma: 0.5,
                k: 1.5,
                a: 1.0,
                gamma: 0.1,
                quote_qty: 1,
                max_abs_inventory: 100,
            });
            let r = engine::run(evs.into_iter(), &mut s, &cfg()).unwrap();
            criterion::black_box(r.fills.len());
        });
    });
    g.finish();
}

criterion_group!(benches, bench_replay_symmetric, bench_replay_as);
criterion_main!(benches);
