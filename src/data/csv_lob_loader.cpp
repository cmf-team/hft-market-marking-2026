#include "bt/csv_lob_loader.hpp"

#include "csv_util.hpp"

#include <stdexcept>
#include <string>

namespace bt {

CsvLobLoader::CsvLobLoader(const std::string& path, InstrumentSpec spec)
    : spec_(spec), file_(path), path_(path) {
    if (!file_) {
        throw std::runtime_error("CsvLobLoader: cannot open " + path);
    }
    if (!std::getline(file_, line_)) {
        throw std::runtime_error("CsvLobLoader: empty file " + path);
    }
}

bool CsvLobLoader::next(BookSnapshot& out) {
    if (!std::getline(file_, line_)) return false;
    ++row_;

    detail::FieldIter it(line_);

    // Skip leading unnamed index column.
    (void)it.next();

    out.ts = detail::parse_i64(it.next(), path_, row_, "local_timestamp");

    // Levels are interleaved per index: ask price/amount, bid price/amount.
    for (std::size_t i = 0; i < kMaxLevels; ++i) {
        const auto ap_sv = it.next();
        const auto aa_sv = it.next();
        const auto bp_sv = it.next();
        const auto ba_sv = it.next();

        const std::string lvl = std::to_string(i);
        const std::string ap_name = "asks[" + lvl + "].price";
        const std::string aa_name = "asks[" + lvl + "].amount";
        const std::string bp_name = "bids[" + lvl + "].price";
        const std::string ba_name = "bids[" + lvl + "].amount";

        if (ap_sv.empty() || aa_sv.empty() || bp_sv.empty() || ba_sv.empty()) {
            detail::throw_parse_error(
                path_, row_,
                "missing field at level " + lvl);
        }

        const double ap = detail::parse_double(ap_sv, path_, row_, ap_name);
        const double aa = detail::parse_double(aa_sv, path_, row_, aa_name);
        const double bp = detail::parse_double(bp_sv, path_, row_, bp_name);
        const double ba = detail::parse_double(ba_sv, path_, row_, ba_name);

        if (!is_on_tick_grid(ap, spec_)) {
            detail::throw_parse_error(
                path_, row_,
                "off-grid " + ap_name + " '" + std::string(ap_sv) + "'");
        }
        if (!is_on_tick_grid(bp, spec_)) {
            detail::throw_parse_error(
                path_, row_,
                "off-grid " + bp_name + " '" + std::string(bp_sv) + "'");
        }

        out.asks[i].price  = to_ticks(ap, spec_);
        out.asks[i].amount = to_qty(aa, spec_);
        out.bids[i].price  = to_ticks(bp, spec_);
        out.bids[i].amount = to_qty(ba, spec_);
    }

    return true;
}

}  // namespace bt
