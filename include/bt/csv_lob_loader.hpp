#pragma once

#include "bt/events.hpp"
#include "bt/types.hpp"

#include <fstream>
#include <string>

namespace bt {

// Streams 25-level L2 book snapshots from a CSV file. Schema (interleaved
// per level, ascending by index):
//
//   ,local_timestamp,
//   asks[0].price,asks[0].amount,bids[0].price,bids[0].amount,
//   asks[1].price,asks[1].amount,bids[1].price,bids[1].amount,
//   ...
//   asks[24].price,asks[24].amount,bids[24].price,bids[24].amount
//
// Prices are snapped to the integer-tick grid via `spec`. Off-grid prices and
// malformed rows throw with the row number and offending field in the message.
class CsvLobLoader {
public:
    CsvLobLoader(const std::string& path, InstrumentSpec spec);

    // Reads the next snapshot. Returns false at EOF, true on success.
    // Throws std::runtime_error on parse / data-quality errors.
    bool next(BookSnapshot& out);

    [[nodiscard]] std::size_t row() const noexcept { return row_; }

private:
    InstrumentSpec spec_;
    std::ifstream  file_;
    std::string    line_;
    std::string    path_;
    std::size_t    row_ = 0;
};

}  // namespace bt
