use clap::Parser;
use hft_mm_backtester::config::Config;
use hft_mm_backtester::engine::{self, EngineCfg};
use hft_mm_backtester::parser::{LobReader, MergedEvents, TradeReader};
use hft_mm_backtester::report::{write_equity_curve_csv, write_fills_csv, write_summary_json};
use hft_mm_backtester::strategy::avellaneda_stoikov::{AsParams, AvellanedaStoikov};
use hft_mm_backtester::strategy::symmetric::SymmetricMM;
use std::fs::File;
use std::io::BufReader;
use std::path::PathBuf;

#[derive(Parser)]
#[command(about = "Run a single backtest from a TOML config")]
struct Args {
    #[arg(short, long)]
    config: PathBuf,
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    env_logger::init();
    let args = Args::parse();
    let cfg = Config::from_path(&args.config)?;
    std::fs::create_dir_all(&cfg.output.dir)?;

    let lob = LobReader::new(BufReader::new(File::open(&cfg.data.lob_csv)?))?;
    let tr = TradeReader::new(BufReader::new(File::open(&cfg.data.trades_csv)?))?;
    let merged = MergedEvents::new(lob, tr);

    let ecfg = EngineCfg {
        tick_ms: cfg.engine.tick_ms,
        allow_partial_fills: cfg.engine.allow_partial_fills,
        start_us: cfg.engine.start_us,
        end_us: cfg.engine.end_us,
    };

    let strat_name = cfg.strategy.kind.clone();
    let run = match strat_name.as_str() {
        "symmetric" => {
            let s = cfg.strategy.symmetric.as_ref().expect("validated");
            let mut strat =
                SymmetricMM::new(s.half_spread.parse()?, s.quote_qty, s.max_abs_inventory);
            engine::run(merged, &mut strat, &ecfg)?
        }
        "avellaneda_stoikov" => {
            let a = cfg.strategy.avellaneda_stoikov.as_ref().expect("validated");
            let mut strat = AvellanedaStoikov::new(AsParams {
                sigma: a.sigma,
                k: a.k,
                a: a.a,
                gamma: a.gamma,
                quote_qty: a.quote_qty,
                max_abs_inventory: a.max_abs_inventory,
            });
            engine::run(merged, &mut strat, &ecfg)?
        }
        _ => unreachable!("validated"),
    };

    if cfg.output.fills {
        write_fills_csv(&cfg.output.dir.join("fills.csv"), &run.fills)?;
    }
    if cfg.output.equity_curve {
        write_equity_curve_csv(
            &cfg.output.dir.join("equity.csv"),
            &run.metrics.equity_curve,
        )?;
    }
    write_summary_json(&cfg.output.dir.join("summary.json"), &strat_name, &run)?;
    println!("OK → {}", cfg.output.dir.display());
    Ok(())
}
