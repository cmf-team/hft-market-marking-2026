use crate::fill_engine::{Fill, FillEngine};
use crate::market_book::MarketBook;
use crate::metrics::Metrics;
use crate::own_orders::OwnOrders;
use crate::parser::{MdEvent, ParseError};
use crate::strategy::{OrderAction, Strategy, TickCtx};
use crate::types::{Side, Ts};

pub struct EngineCfg {
    pub tick_ms: u64,
    pub allow_partial_fills: bool,
    pub start_us: Option<u64>,
    pub end_us: Option<u64>,
}

pub struct Run {
    pub fills: Vec<Fill>,
    pub metrics: Metrics,
    pub last_mid: Option<crate::types::Px>,
    pub session_start_us: Ts,
    pub session_end_us: Ts,
}

pub fn run<I, S>(events: I, strategy: &mut S, cfg: &EngineCfg) -> Result<Run, ParseError>
where
    I: Iterator<Item = Result<MdEvent, ParseError>>,
    S: Strategy,
{
    let mut book = MarketBook::new();
    let mut own = OwnOrders::new();
    let fe = FillEngine::new(cfg.allow_partial_fills);
    let mut metrics = Metrics::new();
    let mut all_fills = Vec::new();

    let tick_us = cfg.tick_ms * 1000;
    let mut session_start: Option<Ts> = None;
    let mut next_tick: Option<Ts> = None;
    let mut started = false;
    let mut last_ts: Ts = 0;

    for ev in events {
        // TODO: bad row appears halfway through the input
        let ev = ev?;
        let ts = match &ev {
            MdEvent::BookUpdate(b) => b.ts_us,
            MdEvent::TradePrint(t) => t.ts_us,
        };

        if let Some(s) = cfg.start_us {
            if ts < s {
                continue;
            }
        }

        if let Some(e) = cfg.end_us {
            if ts > e {
                break;
            }
        }

        last_ts = ts;
        if session_start.is_none() {
            session_start = Some(ts);
        }

        if started {
            let timing = TickTiming {
                tick_us,
                session_start: session_start.unwrap(),
                end_us: cfg.end_us,
            };
            drain_due_ticks(
                strategy,
                &book,
                &mut own,
                &mut metrics,
                &mut next_tick,
                ts,
                timing,
            );
        }

        match ev {
            MdEvent::BookUpdate(b) => {
                book.apply(b);
                // Fill engine on every book update
                let fills = fe.match_book(ts, &book, &mut own);
                for f in &fills {
                    metrics.on_fill(f);
                    strategy.on_fill(f, metrics.position);
                }
                all_fills.extend(fills);
            }
            MdEvent::TradePrint(_) => { /* ignored at runtime */ }
        }

        // Tick scheduling
        if !started {
            if book.is_ready() {
                started = true;
                strategy.on_start(&TickCtx {
                    ts_us: ts,
                    book: &book,
                    own: &own,
                    position: metrics.position,
                    elapsed_ms: 0,
                    session_total_ms: 0,
                });
                next_tick = Some(ts);
            } else {
                continue;
            }
        }

        let timing = TickTiming {
            tick_us,
            session_start: session_start.unwrap(),
            end_us: cfg.end_us,
        };

        drain_due_ticks(
            strategy,
            &book,
            &mut own,
            &mut metrics,
            &mut next_tick,
            ts,
            timing,
        );
    }

    Ok(Run {
        fills: all_fills,
        metrics,
        last_mid: book.mid(),
        session_start_us: session_start.unwrap_or(0),
        session_end_us: last_ts,
    })
}

#[derive(Clone, Copy)]
struct TickTiming {
    tick_us: Ts,
    session_start: Ts,
    end_us: Option<Ts>,
}

fn drain_due_ticks<S: Strategy>(
    strategy: &mut S,
    book: &MarketBook,
    own: &mut OwnOrders,
    metrics: &mut Metrics,
    next_tick: &mut Option<Ts>,
    up_to_ts: Ts,
    timing: TickTiming,
) {
    while let Some(due) = *next_tick {
        if due > up_to_ts {
            break;
        }

        let elapsed_ms = due.saturating_sub(timing.session_start) / 1000;
        let total_ms = match timing.end_us {
            Some(e) if e >= timing.session_start => (e - timing.session_start) / 1000,
            _ => elapsed_ms.max(1),
        };

        let ctx = TickCtx {
            ts_us: due,
            book,
            own,
            position: metrics.position,
            elapsed_ms,
            session_total_ms: total_ms.max(1),
        };

        let actions = strategy.on_tick(&ctx);
        apply_actions(&actions, own, due);

        if let Some(m) = book.mid() {
            metrics.sample(due, m);
        }
        *next_tick = Some(due + timing.tick_us);
    }
}

fn apply_actions(actions: &[OrderAction], own: &mut OwnOrders, ts: Ts) {
    for a in actions {
        match a {
            OrderAction::Place { side, price, qty } => {
                own.place(*side, *price, *qty, ts);
            }
            OrderAction::Cancel { id } => {
                own.cancel(*id);
            }
            OrderAction::CancelAllSide { side } => {
                own.cancel_all_side(*side);
            }
            OrderAction::CancelAll => {
                own.cancel_all_side(Side::Bid);
                own.cancel_all_side(Side::Ask);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::lob::BookSnapshot;
    use crate::strategy::symmetric::SymmetricMM;
    use crate::types::Px;

    fn px(s: &str) -> Px {
        Px(s.parse().unwrap())
    }

    struct RecordingStrategy {
        mids: Vec<(Ts, Px)>,
    }

    impl Strategy for RecordingStrategy {
        fn name(&self) -> &str {
            "recording"
        }

        fn on_tick(&mut self, ctx: &TickCtx) -> Vec<OrderAction> {
            if let Some(mid) = ctx.book.mid() {
                self.mids.push((ctx.ts_us, mid));
            }
            Vec::new()
        }
    }

    #[test]
    fn engine_runs_and_yields_fills_on_cross() {
        let evs: Vec<Result<MdEvent, ParseError>> = vec![
            Ok(MdEvent::BookUpdate(BookSnapshot {
                ts_us: 1_000,
                seq: 1,
                bids: vec![(px("99"), 10)],
                asks: vec![(px("101"), 10)],
            })),
            Ok(MdEvent::BookUpdate(BookSnapshot {
                ts_us: 200_000,
                seq: 2,
                bids: vec![(px("99"), 10)],
                asks: vec![(px("99.5"), 5)],
            })),
        ];
        let mut s = SymmetricMM::new("0.5".parse().unwrap(), 1, 100);
        let cfg = EngineCfg {
            tick_ms: 100,
            allow_partial_fills: true,
            start_us: None,
            end_us: Some(200_000),
        };
        let run = run(evs.into_iter(), &mut s, &cfg).unwrap();
        assert!(!run.fills.is_empty());
        assert_eq!(run.fills[0].side, Side::Bid);
        assert_eq!(run.fills[0].price, px("99.5"));
    }

    #[test]
    fn engine_respects_window() {
        let evs: Vec<Result<MdEvent, ParseError>> = vec![
            Ok(MdEvent::BookUpdate(BookSnapshot {
                ts_us: 100,
                seq: 1,
                bids: vec![(px("99"), 5)],
                asks: vec![(px("101"), 5)],
            })),
            Ok(MdEvent::BookUpdate(BookSnapshot {
                ts_us: 9_999_999,
                seq: 2,
                bids: vec![(px("99"), 5)],
                asks: vec![(px("101"), 5)],
            })),
        ];
        let mut s = SymmetricMM::new("0.5".parse().unwrap(), 1, 100);
        let cfg = EngineCfg {
            tick_ms: 100,
            allow_partial_fills: true,
            start_us: Some(50),
            end_us: Some(1_000_000),
        };
        let run = run(evs.into_iter(), &mut s, &cfg).unwrap();
        assert_eq!(run.session_end_us, 100); // second event filtered
    }

    #[test]
    fn first_tick_gated_on_ready_book() {
        let evs: Vec<Result<MdEvent, ParseError>> = vec![
            Ok(MdEvent::BookUpdate(BookSnapshot {
                ts_us: 1,
                seq: 1,
                bids: vec![(px("99"), 5)],
                asks: vec![],
            })),
            Ok(MdEvent::BookUpdate(BookSnapshot {
                ts_us: 200_000,
                seq: 2,
                bids: vec![(px("99"), 5)],
                asks: vec![(px("101"), 5)],
            })),
        ];
        let mut s = SymmetricMM::new("0.5".parse().unwrap(), 1, 100);
        let cfg = EngineCfg {
            tick_ms: 100,
            allow_partial_fills: true,
            start_us: None,
            end_us: Some(300_000),
        };
        let run = run(evs.into_iter(), &mut s, &cfg).unwrap();
        assert!(!run.metrics.equity_curve.is_empty());
    }

    #[test]
    fn overdue_ticks_use_previous_book_state() {
        let evs: Vec<Result<MdEvent, ParseError>> = vec![
            Ok(MdEvent::BookUpdate(BookSnapshot {
                ts_us: 0,
                seq: 1,
                bids: vec![(px("100"), 5)],
                asks: vec![(px("102"), 5)],
            })),
            Ok(MdEvent::BookUpdate(BookSnapshot {
                ts_us: 200_000,
                seq: 2,
                bids: vec![(px("200"), 5)],
                asks: vec![(px("202"), 5)],
            })),
        ];
        let mut s = RecordingStrategy { mids: Vec::new() };
        let cfg = EngineCfg {
            tick_ms: 100,
            allow_partial_fills: true,
            start_us: None,
            end_us: Some(200_000),
        };

        let _ = run(evs.into_iter(), &mut s, &cfg).unwrap();

        assert_eq!(
            s.mids,
            vec![(0, px("101")), (100_000, px("101")), (200_000, px("101")),]
        );
    }
}
