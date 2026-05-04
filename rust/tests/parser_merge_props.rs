//! Property tests for `MergedEvents`: a 2-way time-ordered iterator over
//! `BookSnapshot`s and `TradePrint`s.
//!
//! Invariants:
//!   1. Output length = #lob + #trades (no events lost or duplicated).
//!   2. Output timestamps are non-decreasing.
//!   3. The relative order of events within each input is preserved
//!      (stability w.r.t. each source stream).
//!   4. On equal timestamps, `BookUpdate` is yielded before `TradePrint`
//!      (documented tie-break, design §5.2).
//!   5. Either input being empty just streams the other.

use hft_mm_backtester::parser::{
    lob::BookSnapshot, trades::TradePrint, MdEvent, MergedEvents, ParseError,
};
use hft_mm_backtester::types::{Px, Side};
use proptest::prelude::*;
use rust_decimal::Decimal;

fn snap(ts: u64) -> BookSnapshot {
    BookSnapshot {
        ts_us: ts,
        seq: 0,
        bids: vec![],
        asks: vec![],
    }
}
fn trade(ts: u64) -> TradePrint {
    TradePrint {
        ts_us: ts,
        side: Side::Bid,
        price: Px(Decimal::ONE),
        size: 1,
    }
}

/// Produce a sorted-ascending vector of timestamps from a strategy seed.
fn sorted_ts(mut v: Vec<u64>) -> Vec<u64> {
    v.sort();
    v
}

proptest! {
    /// Merging preserves total count and outputs non-decreasing timestamps.
    #[test]
    fn count_and_order(
        lob_ts in proptest::collection::vec(0u64..1_000_000, 0..50),
        trd_ts in proptest::collection::vec(0u64..1_000_000, 0..50),
    ) {
        let lob_ts = sorted_ts(lob_ts);
        let trd_ts = sorted_ts(trd_ts);

        let lob_iter: Vec<Result<BookSnapshot, ParseError>> =
            lob_ts.iter().copied().map(|t| Ok(snap(t))).collect();
        let trd_iter: Vec<Result<TradePrint, ParseError>> =
            trd_ts.iter().copied().map(|t| Ok(trade(t))).collect();

        let merged: Vec<MdEvent> = MergedEvents::new(lob_iter.into_iter(), trd_iter.into_iter())
            .collect::<Result<_,_>>().unwrap();

        prop_assert_eq!(merged.len(), lob_ts.len() + trd_ts.len());

        let ts: Vec<u64> = merged.iter().map(|e| e.ts_us()).collect();
        for w in ts.windows(2) {
            prop_assert!(w[0] <= w[1], "timestamps must be non-decreasing");
        }
    }

    /// Stability per input stream: events from `lob` appear in the merged
    /// output in their original order, and likewise for `trades`.
    #[test]
    fn per_stream_stability(
        lob_ts in proptest::collection::vec(0u64..1_000_000, 0..50),
        trd_ts in proptest::collection::vec(0u64..1_000_000, 0..50),
    ) {
        let lob_ts = sorted_ts(lob_ts);
        let trd_ts = sorted_ts(trd_ts);

        let lob_iter: Vec<Result<BookSnapshot, ParseError>> =
            lob_ts.iter().copied().map(|t| Ok(snap(t))).collect();
        let trd_iter: Vec<Result<TradePrint, ParseError>> =
            trd_ts.iter().copied().map(|t| Ok(trade(t))).collect();

        let merged: Vec<MdEvent> = MergedEvents::new(lob_iter.into_iter(), trd_iter.into_iter())
            .collect::<Result<_,_>>().unwrap();

        let extracted_lob: Vec<u64> = merged.iter()
            .filter_map(|e| match e { MdEvent::BookUpdate(b) => Some(b.ts_us), _ => None })
            .collect();
        let extracted_trd: Vec<u64> = merged.iter()
            .filter_map(|e| match e { MdEvent::TradePrint(t) => Some(t.ts_us), _ => None })
            .collect();

        prop_assert_eq!(extracted_lob, lob_ts);
        prop_assert_eq!(extracted_trd, trd_ts);
    }

    /// Tie-break: when a lob ts equals a trade ts, BookUpdate comes first.
    /// We pick a small set of fixed timestamps and intersperse identical ones.
    #[test]
    fn book_before_trade_at_equal_ts(
        shared in proptest::collection::vec(0u64..100, 1..10),
    ) {
        let lob_iter: Vec<Result<BookSnapshot, ParseError>> =
            shared.iter().copied().map(|t| Ok(snap(t))).collect();
        let trd_iter: Vec<Result<TradePrint, ParseError>> =
            shared.iter().copied().map(|t| Ok(trade(t))).collect();

        let merged: Vec<MdEvent> = MergedEvents::new(lob_iter.into_iter(), trd_iter.into_iter())
            .collect::<Result<_,_>>().unwrap();

        // For every consecutive pair with equal timestamps, the first must be
        // a BookUpdate and the second a TradePrint.
        for w in merged.windows(2) {
            if w[0].ts_us() == w[1].ts_us()
                && matches!(w[0], MdEvent::TradePrint(_))
                && matches!(w[1], MdEvent::BookUpdate(_))
            {
                prop_assert!(false, "tie-break violated: trade before book at equal ts");
            }
        }
    }

    /// Empty lob: passthrough of trades.
    #[test]
    fn empty_lob_passthrough(
        trd_ts in proptest::collection::vec(0u64..1_000_000, 0..50),
    ) {
        let trd_ts = sorted_ts(trd_ts);
        let lob_iter: std::iter::Empty<Result<BookSnapshot, ParseError>> = std::iter::empty();
        let trd_iter: Vec<Result<TradePrint, ParseError>> =
            trd_ts.iter().copied().map(|t| Ok(trade(t))).collect();

        let merged: Vec<MdEvent> = MergedEvents::new(lob_iter, trd_iter.into_iter())
            .collect::<Result<_,_>>().unwrap();

        prop_assert_eq!(merged.len(), trd_ts.len());
        prop_assert!(merged.iter().all(|e| matches!(e, MdEvent::TradePrint(_))));
    }

    /// Empty trades: passthrough of lob.
    #[test]
    fn empty_trades_passthrough(
        lob_ts in proptest::collection::vec(0u64..1_000_000, 0..50),
    ) {
        let lob_ts = sorted_ts(lob_ts);
        let trd_iter: std::iter::Empty<Result<TradePrint, ParseError>> = std::iter::empty();
        let lob_iter: Vec<Result<BookSnapshot, ParseError>> =
            lob_ts.iter().copied().map(|t| Ok(snap(t))).collect();

        let merged: Vec<MdEvent> = MergedEvents::new(lob_iter.into_iter(), trd_iter)
            .collect::<Result<_,_>>().unwrap();

        prop_assert_eq!(merged.len(), lob_ts.len());
        prop_assert!(merged.iter().all(|e| matches!(e, MdEvent::BookUpdate(_))));
    }
}
