#pragma once

// Shared helpers for tests that need to write a 25-level LOB CSV fixture.
// Used by test_csv_lob_loader.cpp and test_event_stream.cpp.

#include "bt/events.hpp"

#include <cstdint>
#include <sstream>
#include <string>

namespace bt::testing {

// Build the LOB header line in the user's column order:
//   ,local_timestamp,asks[0].price,asks[0].amount,bids[0].price,bids[0].amount,...
inline std::string make_lob_header() {
    std::ostringstream s;
    s << ",local_timestamp";
    for (std::size_t i = 0; i < kMaxLevels; ++i) {
        s << ",asks[" << i << "].price"
          << ",asks[" << i << "].amount"
          << ",bids[" << i << "].price"
          << ",bids[" << i << "].amount";
    }
    s << '\n';
    return s.str();
}

// Build a single row with synthetic but on-grid prices:
//   asks[i] = (110436 + i) * 1e-7,  amounts = (i+1) * 100
//   bids[i] = (110435 - i) * 1e-7,  amounts = (i+1) * 200
// Each level has a unique price within the row, which makes targeted
// substring replacement (for negative tests) safe.
inline std::string make_lob_row(int idx, std::int64_t ts) {
    std::ostringstream s;
    s << idx << ',' << ts;
    s.precision(7);
    s << std::fixed;
    for (std::size_t i = 0; i < kMaxLevels; ++i) {
        const double ap = (110436 + static_cast<double>(i)) * 1e-7;
        const double bp = (110435 - static_cast<double>(i)) * 1e-7;
        const auto   aa = static_cast<double>((i + 1) * 100);
        const auto   ba = static_cast<double>((i + 1) * 200);
        s << ',' << ap << ',' << aa << ',' << bp << ',' << ba;
    }
    s << '\n';
    return s.str();
}

}  // namespace bt::testing
