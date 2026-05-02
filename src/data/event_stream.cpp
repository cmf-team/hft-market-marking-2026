#include "data/event_stream.hpp"

namespace cmf {

EventStream::EventStream(const std::string& book_path, const std::string& trades_path)
    : book_reader_(book_path), trade_reader_(trades_path) {}

void EventStream::prime_book() {
    if (have_staged_book_) return;
    have_staged_book_ = book_reader_.next(staged_book_);
}

void EventStream::prime_trade() {
    if (have_staged_trade_) return;
    have_staged_trade_ = trade_reader_.next(staged_trade_);
}

bool EventStream::next() {
    prime_book();
    prime_trade();

    if (!have_staged_book_ && !have_staged_trade_) {
        return false;
    }

    // Tie-break: book before trade at equal timestamps.
    bool take_book;
    if (!have_staged_trade_) {
        take_book = true;
    } else if (!have_staged_book_) {
        take_book = false;
    } else {
        take_book = staged_book_.timestamp() <= staged_trade_.ts;
    }

    if (take_book) {
        book_ = staged_book_;
        have_staged_book_ = false;
        type_ = Type::BookUpdate;
        ++book_events_;
    } else {
        last_trade_ = staged_trade_;
        have_staged_trade_ = false;
        type_ = Type::Trade;
        ++trade_events_;
    }
    return true;
}

NanoTime EventStream::current_ts() const noexcept {
    return type_ == Type::BookUpdate ? book_.timestamp() : last_trade_.ts;
}

}  // namespace cmf
