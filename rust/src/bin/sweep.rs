use clap::Parser;
use hft_mm_backtester::config::Config;
use hft_mm_backtester::engine::{self, EngineCfg};
use hft_mm_backtester::parser::{LobReader, MergedEvents, TradeReader};
use hft_mm_backtester::strategy::avellaneda_stoikov::{AsParams, AvellanedaStoikov};
use rayon::prelude::*;
use std::fs::File;
use std::io::{BufReader, Write};
use std::path::PathBuf;

type SweepRow = (f64, String, i64, String, u64);

#[derive(Parser)]
#[command(about = "γ-sweep for Avellaneda–Stoikov in parallel")]
struct Args {
    #[arg(short, long)]
    config: PathBuf,
    #[arg(long)]
    gammas: String, // "0.01,0.05,0.1,0.5,1.0"
    #[arg(long, default_value = "sweep.csv")]
    out: PathBuf,
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args = Args::parse();
    let cfg = Config::from_path(&args.config)?;
    let base = cfg
        .strategy
        .avellaneda_stoikov
        .as_ref()
        .ok_or("config must use kind=avellaneda_stoikov")?
        .clone();

    let gammas: Vec<f64> = args
        .gammas
        .split(',')
        .map(|s| s.trim().parse::<f64>())
        .collect::<Result<_, _>>()?;

    let cfg_arc = std::sync::Arc::new(cfg);

    let rows: Vec<SweepRow> = gammas
        .par_iter()
        .map(|&g| -> Result<SweepRow, String> {
            let cfg = cfg_arc.clone();
            let lob_file = File::open(&cfg.data.lob_csv)
                .map_err(|e| format!("gamma {g}: open {}: {e}", cfg.data.lob_csv.display()))?;
            let trades_file = File::open(&cfg.data.trades_csv)
                .map_err(|e| format!("gamma {g}: open {}: {e}", cfg.data.trades_csv.display()))?;
            let lob = LobReader::new(BufReader::new(lob_file))
                .map_err(|e| format!("gamma {g}: read LOB CSV: {e}"))?;
            let tr = TradeReader::new(BufReader::new(trades_file))
                .map_err(|e| format!("gamma {g}: read trades CSV: {e}"))?;
            let merged = MergedEvents::new(lob, tr);
            let mut strat = AvellanedaStoikov::new(AsParams {
                sigma: base.sigma,
                k: base.k,
                a: base.a,
                gamma: g,
                quote_qty: base.quote_qty,
                max_abs_inventory: base.max_abs_inventory,
            });
            let ecfg = EngineCfg {
                tick_ms: cfg.engine.tick_ms,
                allow_partial_fills: cfg.engine.allow_partial_fills,
                start_us: cfg.engine.start_us,
                end_us: cfg.engine.end_us,
            };
            let r = engine::run(merged, &mut strat, &ecfg)
                .map_err(|e| format!("gamma {g}: engine run: {e}"))?;
            let s = r.metrics.summary(r.last_mid);
            Ok((
                g,
                s.final_equity.map(|d| d.to_string()).unwrap_or_default(),
                s.final_position,
                s.turnover_notional.to_string(),
                s.trade_count,
            ))
        })
        .collect::<Result<_, _>>()
        .map_err(std::io::Error::other)?;

    let mut f = File::create(&args.out)?;
    writeln!(
        f,
        "gamma,final_equity,final_position,turnover_notional,trade_count"
    )?;
    for (g, eq, pos, t, n) in rows {
        writeln!(f, "{},{},{},{},{}", g, eq, pos, t, n)?;
    }
    println!("OK → {}", args.out.display());
    Ok(())
}
