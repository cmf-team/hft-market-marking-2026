#pragma once

#include "book/order_book.hpp"
#include "core/event.hpp"
#include "data/csv_reader.hpp"

#include <cstddef>
#include <string>

namespace cmf {

// Chronologically merges a book-snapshot stream and a trade stream.
// On each next() call, exactly one event is consumed; the book reflects
// the most recently applied snapshot. Trade events do not mutate the book.
class EventStream {
public:
    enum class Type {
        BookUpdate,
        Trade,
    };

    EventStream(const std::string& book_path, const std::string& trades_path);

    // Advances to the next event. Returns false when both streams are exhausted.
    bool next();

    Type             current_type() const noexcept { return type_; }
    NanoTime    current_ts()   const noexcept;
    const OrderBook& book()         const noexcept { return book_; }
    const Trade&     trade()        const noexcept { return last_trade_; }

    std::size_t book_events()  const noexcept { return book_events_; }
    std::size_t trade_events() const noexcept { return trade_events_; }

private:
    void prime_book();
    void prime_trade();

    LobCsvReader    book_reader_;
    TradesCsvReader trade_reader_;

    OrderBook book_{};         // live state, mutated on BookUpdate events
    OrderBook staged_book_{};  // pre-read peek
    bool      have_staged_book_{false};

    Trade staged_trade_{};
    bool  have_staged_trade_{false};

    Type  type_{Type::BookUpdate};
    Trade last_trade_{};

    std::size_t book_events_{0};
    std::size_t trade_events_{0};
};

}  // namespace cmf
