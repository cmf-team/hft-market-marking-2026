#pragma once

#include "bt/events.hpp"
#include "bt/types.hpp"

#include <fstream>
#include <string>

namespace bt {

// Streams trade events from a CSV file with the schema:
//   ,local_timestamp,side,price,amount
//
// Prices are parsed as doubles and immediately snapped to the integer-tick
// grid using `spec`. Off-grid prices and malformed rows throw with the row
// number and offending field included in the message.
class CsvTradeLoader {
public:
    CsvTradeLoader(const std::string& path, InstrumentSpec spec);

    // Reads the next trade. Returns false at EOF, true on success.
    // Throws std::runtime_error on parse / data-quality errors.
    bool next(Trade& out);

    [[nodiscard]] std::size_t row() const noexcept { return row_; }

private:
    InstrumentSpec spec_;
    std::ifstream  file_;
    std::string    line_;
    std::string    path_;
    std::size_t    row_ = 0;  // 1-based, counted from the first data row
};

}  // namespace bt
