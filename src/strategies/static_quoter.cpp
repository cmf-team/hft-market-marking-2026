#include "bt/static_quoter.hpp"

#include "bt/exchange.hpp"

namespace bt {

void StaticQuoter::on_book(const OrderBook& book, Timestamp now) {
    if (book.empty() || exchange_ == nullptr) return;

    const Price desired_buy  = book.best_bid() - 1;
    const Price desired_sell = book.best_ask() + 1;

    refresh_side_(Side::Buy,  desired_buy,  now);
    refresh_side_(Side::Sell, desired_sell, now);
}

void StaticQuoter::refresh_side_(Side side, Price desired, Timestamp now) {
    QuoteState& q = (side == Side::Buy) ? buy_ : sell_;

    // While a submit is in flight we can't do anything sensible — wait for
    // the ack (or reject) before deciding to repost.
    if (q.pending) return;

    // Already resting at the desired price → nothing to do.
    if (q.resting_id != 0 && q.intended_px == desired) return;

    // Need to refresh: cancel the current resting order (if any), then
    // submit a fresh one. The cancel and submit can be in flight at once;
    // we just clear our local resting_id immediately so the next on_book
    // doesn't try to cancel it again.
    if (q.resting_id != 0) {
        exchange_->cancel(now, q.resting_id);
        q.resting_id = 0;
    }
    exchange_->post_only_limit(now, side, desired, quote_size_);
    q.pending     = true;
    q.intended_px = desired;
    pending_acks_.push_back(side);
}

void StaticQuoter::on_submitted(OrderId id) {
    if (pending_acks_.empty()) return;
    const Side s = pending_acks_.front();
    pending_acks_.pop_front();
    QuoteState& q = (s == Side::Buy) ? buy_ : sell_;
    q.resting_id = id;
    q.pending    = false;
}

void StaticQuoter::on_fill(const Fill& fill) {
    // Resting order is gone (or partially gone — for v1's static quoter we
    // don't track partial fills, the next on_book will repost regardless).
    if (fill.side == Side::Buy && fill.id == buy_.resting_id) {
        buy_.resting_id  = 0;
        buy_.intended_px = 0;
    } else if (fill.side == Side::Sell && fill.id == sell_.resting_id) {
        sell_.resting_id  = 0;
        sell_.intended_px = 0;
    }
}

void StaticQuoter::on_reject(const OrderReject& /*reject*/) {
    // Post-only would-cross at delivery time. The market moved into our
    // intended price during the latency window. Pop the matching pending
    // entry and clear intent so the next on_book attempts a fresh quote.
    if (pending_acks_.empty()) return;
    const Side s = pending_acks_.front();
    pending_acks_.pop_front();
    QuoteState& q = (s == Side::Buy) ? buy_ : sell_;
    q.pending     = false;
    q.intended_px = 0;
}

void StaticQuoter::on_cancel_ack(OrderId /*id*/) {
    // We already cleared resting_id when we issued the cancel. The ack is
    // purely informational for this strategy.
}

void StaticQuoter::on_cancel_reject(const CancelReject& /*reject*/) {
    // The order was already gone (filled in flight). on_fill has already
    // cleared the relevant state, so nothing more to do.
}

}  // namespace bt
