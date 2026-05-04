use clap::Parser;
use hft_mm_backtester::calibration::{fit_a_k, sigma_from_log_returns};
use hft_mm_backtester::parser::{LobReader, MdEvent, MergedEvents, TradeReader};
use rust_decimal::prelude::ToPrimitive;
use std::collections::BTreeMap;
use std::fs::File;
use std::io::BufReader;
use std::path::PathBuf;

#[derive(Parser)]
#[command(about = "Calibrate sigma, A, k from LOB and trades CSV")]
struct Args {
    #[arg(long)]
    lob: PathBuf,
    #[arg(long)]
    trades: PathBuf,
    /// Sample mid every N microseconds for sigma
    #[arg(long, default_value_t = 1_000_000)]
    sigma_sample_us: u64,
    /// Distance bucket width for (A,k) regression (price units)
    #[arg(long, default_value_t = 0.0001)]
    bucket: f64,
    /// Number of buckets (1..=N)
    #[arg(long, default_value_t = 20)]
    n_buckets: usize,
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let a = Args::parse();
    let lob = LobReader::new(BufReader::new(File::open(&a.lob)?))?;
    let tr = TradeReader::new(BufReader::new(File::open(&a.trades)?))?;

    let mut sigma_samples: Vec<f64> = Vec::new();
    let mut last_sample_ts: u64 = 0;
    let mut current_mid: Option<f64> = None;
    let mut counts: BTreeMap<usize, u64> = BTreeMap::new();
    let mut total_us: u64 = 0;
    let mut first_ts: Option<u64> = None;
    let mut last_ts: u64 = 0;

    for ev in MergedEvents::new(lob, tr) {
        match ev? {
            MdEvent::BookUpdate(b) => {
                if let (Some(bb), Some(ba)) = (b.bids.first(), b.asks.first()) {
                    let m = (bb.0 .0 + ba.0 .0) / rust_decimal::Decimal::from(2);
                    current_mid = m.to_f64();
                }
                if first_ts.is_none() {
                    first_ts = Some(b.ts_us);
                    last_sample_ts = b.ts_us;
                }
                last_ts = b.ts_us;
                if b.ts_us >= last_sample_ts + a.sigma_sample_us {
                    if let Some(mid) = current_mid {
                        sigma_samples.push(mid);
                    }
                    last_sample_ts = b.ts_us;
                }
            }
            MdEvent::TradePrint(t) => {
                if let Some(mid) = current_mid {
                    let px = t.price.0.to_f64().unwrap_or(0.0);
                    let dist = (px - mid).abs();
                    let bucket = (dist / a.bucket).floor() as usize;
                    if bucket >= 1 && bucket <= a.n_buckets {
                        *counts.entry(bucket).or_insert(0) += 1;
                    }
                }
                last_ts = t.ts_us;
            }
        }
    }
    if let Some(s) = first_ts {
        total_us = last_ts.saturating_sub(s);
    }
    let total_secs = (total_us as f64) / 1_000_000.0;

    let sigma = sigma_from_log_returns(&sigma_samples);
    let mut deltas = Vec::new();
    let mut lambdas = Vec::new();
    for (bucket, count) in &counts {
        let delta = (*bucket as f64) * a.bucket;
        let lam = (*count as f64) / total_secs.max(1e-9);
        deltas.push(delta);
        lambdas.push(lam);
    }
    let fit = fit_a_k(&deltas, &lambdas);
    let (a_, k_, r2) = match fit {
        Some(f) => (f.a, f.k, f.r_squared),
        None => (1.0, 1.0, 0.0),
    };

    println!(
        "# calibrated from {} samples; (A,k) R² = {:.4}",
        sigma_samples.len(),
        r2
    );
    println!("[strategy.avellaneda_stoikov]");
    println!("sigma = {:.10}", sigma);
    println!("k = {:.6}", k_);
    println!("a = {:.6}", a_);
    println!("gamma = 0.1");
    println!("quote_qty = 1");
    println!("max_abs_inventory = 100");
    Ok(())
}
