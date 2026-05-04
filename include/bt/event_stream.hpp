#pragma once

#include "bt/csv_lob_loader.hpp"
#include "bt/csv_trade_loader.hpp"
#include "bt/events.hpp"

#include <variant>

namespace bt {

// A single chronologically-ordered event consumed by the engine.
using Event = std::variant<BookSnapshot, Trade>;

// Convenience: returns the timestamp of any Event without unpacking the
// variant at the call site.
[[nodiscard]] inline Timestamp event_ts(const Event& e) noexcept {
    return std::visit([](const auto& x) { return x.ts; }, e);
}

// Merges a snapshot stream and a trade stream into a single ascending-timestamp
// event stream. Each call to `next(Event&)` peeks the head of both underlying
// loaders and yields whichever is older. Tie-breaker on equal timestamps:
// snapshots come before trades, so the strategy sees the updated book state
// before reacting to a trade printed at the same `local_timestamp`.
//
// The merger is lazy and allocation-free — at most one buffered event per
// source is kept in memory at any time.
class MergedEventStream {
public:
    MergedEventStream(CsvLobLoader& lob, CsvTradeLoader& trades) noexcept
        : lob_(lob), trades_(trades) {}

    // Reads the next event into `out`. Returns false when both sources are
    // exhausted. Propagates any parse exception thrown by the underlying
    // loaders.
    bool next(Event& out);

private:
    CsvLobLoader&   lob_;
    CsvTradeLoader& trades_;
    BookSnapshot    pending_lob_{};
    Trade           pending_trade_{};
    bool            has_lob_   = false;
    bool            has_trade_ = false;
    bool            lob_done_  = false;
    bool            trade_done_ = false;
};

}  // namespace bt
