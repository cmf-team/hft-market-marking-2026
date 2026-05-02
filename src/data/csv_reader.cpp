#include "data/csv_reader.hpp"

#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cmf {

namespace {

double parse_double(std::string_view sv) {
    if (sv.empty()) return 0.0;
    return std::stod(std::string(sv));
}

std::int64_t parse_int64(std::string_view sv) {
    if (sv.empty()) return 0;
    std::int64_t value = 0;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    if (ec != std::errc{}) {
        throw std::runtime_error("csv_reader: failed to parse integer");
    }
    return value;
}

Side parse_side(std::string_view sv) {
    if (sv == "buy")  return Side::Buy;
    if (sv == "sell") return Side::Sell;
    throw std::runtime_error("csv_reader: unknown side value");
}

bool next_field(std::string_view& line, std::string_view& field) {
    if (line.empty()) return false;
    auto comma = line.find(',');
    if (comma == std::string_view::npos) {
        field = line;
        line  = {};
    } else {
        field = line.substr(0, comma);
        line.remove_prefix(comma + 1);
    }
    return true;
}

}  // namespace

// ---------- LobCsvReader ----------

LobCsvReader::LobCsvReader(const std::string& path) : file_(path) {
    if (!file_) {
        throw std::runtime_error("LobCsvReader: cannot open " + path);
    }
    if (!std::getline(file_, header_)) {
        throw std::runtime_error("LobCsvReader: empty file " + path);
    }
}

bool LobCsvReader::next(OrderBook& book) {
    if (!std::getline(file_, line_)) {
        return false;
    }

    std::string_view view{line_};
    std::string_view field;

    // index column (unnamed first column, pandas-style)
    if (!next_field(view, field)) return false;

    // local_timestamp
    if (!next_field(view, field)) return false;
    NanoTime ts = parse_int64(field);

    auto& asks = book.asks();
    auto& bids = book.bids();

    for (std::size_t i = 0; i < OrderBook::kDepth; ++i) {
        if (!next_field(view, field)) { asks[i].price  = 0.0; } else { asks[i].price  = parse_double(field); }
        if (!next_field(view, field)) { asks[i].amount = 0.0; } else { asks[i].amount = parse_double(field); }
        if (!next_field(view, field)) { bids[i].price  = 0.0; } else { bids[i].price  = parse_double(field); }
        if (!next_field(view, field)) { bids[i].amount = 0.0; } else { bids[i].amount = parse_double(field); }
    }

    book.set_timestamp(ts);
    ++rows_read_;
    return true;
}

// ---------- TradesCsvReader ----------

TradesCsvReader::TradesCsvReader(const std::string& path) : file_(path) {
    if (!file_) {
        throw std::runtime_error("TradesCsvReader: cannot open " + path);
    }
    if (!std::getline(file_, header_)) {
        throw std::runtime_error("TradesCsvReader: empty file " + path);
    }
}

bool TradesCsvReader::next(Trade& trade) {
    if (!std::getline(file_, line_)) {
        return false;
    }

    std::string_view view{line_};
    std::string_view field;

    // index column
    if (!next_field(view, field)) return false;

    // local_timestamp
    if (!next_field(view, field)) return false;
    trade.ts = parse_int64(field);

    // side
    if (!next_field(view, field)) return false;
    trade.side = parse_side(field);

    // price
    if (!next_field(view, field)) return false;
    trade.price = parse_double(field);

    // amount
    if (!next_field(view, field)) return false;
    trade.amount = parse_double(field);

    ++rows_read_;
    return true;
}

}  // namespace cmf
