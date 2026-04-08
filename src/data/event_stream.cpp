#include "bt/event_stream.hpp"

namespace bt {

bool MergedEventStream::next(Event& out) {
    // Refill the buffered head of each source if it's not already full and the
    // source isn't exhausted. We track `_done` separately from `has_` so a
    // depleted loader is only consulted once.
    if (!has_lob_ && !lob_done_) {
        if (lob_.next(pending_lob_)) {
            has_lob_ = true;
        } else {
            lob_done_ = true;
        }
    }
    if (!has_trade_ && !trade_done_) {
        if (trades_.next(pending_trade_)) {
            has_trade_ = true;
        } else {
            trade_done_ = true;
        }
    }

    if (!has_lob_ && !has_trade_) return false;

    // Pick the older event. Tie-break: snapshots before trades.
    const bool take_lob =
        has_lob_ && (!has_trade_ || pending_lob_.ts <= pending_trade_.ts);

    if (take_lob) {
        out = pending_lob_;
        has_lob_ = false;
    } else {
        out = pending_trade_;
        has_trade_ = false;
    }
    return true;
}

}  // namespace bt
