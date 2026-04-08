#include "bt/csv_trade_loader.hpp"

#include "csv_util.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace bt {

CsvTradeLoader::CsvTradeLoader(const std::string& path, const InstrumentSpec spec)
    : spec_(spec), file_(path), path_(path) {
    if (!file_) {
        throw std::runtime_error("CsvTradeLoader: cannot open " + path);
    }
    // Drop the header line.
    if (!std::getline(file_, line_)) {
        throw std::runtime_error("CsvTradeLoader: empty file " + path);
    }
}

bool CsvTradeLoader::next(Trade& out) {
    if (!std::getline(file_, line_)) return false;
    ++row_;

    detail::FieldIter it(line_);

    // Skip leading unnamed index column.
    (void)it.next();

    const auto ts_sv    = it.next();
    const auto side_sv  = it.next();
    const auto price_sv = it.next();
    const auto amt_sv   = it.next();

    out.ts = detail::parse_i64(ts_sv, path_, row_, "local_timestamp");

    if (side_sv == "buy") {
        out.side = Side::Buy;
    } else if (side_sv == "sell") {
        out.side = Side::Sell;
    } else {
        detail::throw_parse_error(path_, row_,
                                  "bad side '" + std::string(side_sv) + "'");
    }

    const double price = detail::parse_double(price_sv, path_, row_, "price");
    if (!is_on_tick_grid(price, spec_)) {
        detail::throw_parse_error(path_, row_,
                                  "off-grid price " + std::string(price_sv));
    }
    out.price = to_ticks(price, spec_);

    const double amt = detail::parse_double(amt_sv, path_, row_, "amount");
    out.amount = to_qty(amt, spec_);

    return true;
}

}  // namespace bt
