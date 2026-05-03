#include "csv_reader.hpp"

#include <charconv>
#include <cstdlib>
#include <string_view>
#include <system_error>

namespace hft {

namespace {


void strip_trailing_cr(std::string_view& field) {
    if (!field.empty() && field.back() == '\r') {
        field.remove_suffix(1);
    }
}

bool parse_int64(std::string_view field, std::int64_t& out) {
    strip_trailing_cr(field);
    if (field.empty()) {
        return false;
    }

    const char* begin = field.data();
    const char* end = field.data() + field.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc() && ptr == end;
}

bool parse_double(std::string_view field, double& out) {
    strip_trailing_cr(field);
    if (field.empty()) {
        return false;
    }

    const char* begin = field.data();
    const char* end = field.data() + field.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    if (ec == std::errc() && ptr == end) {
        return true;
    }


    std::string tmp(field);
    char* parse_end = nullptr;
    out = std::strtod(tmp.c_str(), &parse_end);
    return parse_end != nullptr && *parse_end == '\0';
}

bool parse_lob_line(const std::string& line, BookEvent& event) {
    int col = 0;
    std::size_t start = 0;
    bool has_ts = false;
    bool has_ask = false;
    bool has_ask_qty = false;
    bool has_bid = false;
    bool has_bid_qty = false;

    auto parse_field = [&](std::size_t end) {
        std::string_view field(line.data() + start, end - start);

        switch (col) {
            case 1:
                has_ts = parse_int64(field, event.ts);
                break;
            case 2:
                has_ask = parse_double(field, event.best_ask);
                break;
            case 3:
                has_ask_qty = parse_double(field, event.best_ask_qty);
                break;
            case 4:
                has_bid = parse_double(field, event.best_bid);
                break;
            case 5:
                has_bid_qty = parse_double(field, event.best_bid_qty);
                break;
            default:
                break;
        }
    };

    for (std::size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == ',') {
            parse_field(i);
            ++col;
            start = i + 1;
            if (col > 5 && has_ts && has_ask && has_ask_qty && has_bid &&
                has_bid_qty) {
                break;
            }
        }
    }

    return has_ts && has_ask && has_ask_qty && has_bid && has_bid_qty &&
           event.best_bid > 0.0 && event.best_ask > 0.0;
}

bool parse_trade_line(const std::string& line, TradeEvent& event) {
    int col = 0;
    std::size_t start = 0;
    bool has_ts = false;
    bool has_side = false;
    bool has_price = false;
    bool has_qty = false;

    auto parse_field = [&](std::size_t end) {
        std::string_view field(line.data() + start, end - start);
        strip_trailing_cr(field);
        switch (col) {
            case 1: {
                has_ts = parse_int64(field, event.ts);
                break;
            }
            case 2: {
                event.side = side_from_string(std::string(field));
                has_side = true;
                break;
            }
            case 3: {
                has_price = parse_double(field, event.price);
                break;
            }
            case 4: {
                has_qty = parse_double(field, event.qty);
                break;
            }
            default:
                break;
        }
    };

    for (std::size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == ',') {
            parse_field(i);
            ++col;
            start = i + 1;
            if (col > 4 && has_ts && has_side && has_price && has_qty) {
                break;
            }
        }
    }

    return has_ts && has_side && has_price && has_qty && event.price > 0.0 &&
           event.qty > 0.0;
}

}

CsvLobReader::CsvLobReader(std::string path) : path_(std::move(path)) {}

bool CsvLobReader::open() {
    input_.close();
    input_.clear();
    input_.open(path_);
    rows_read_ = 0;
    if (!input_.is_open()) {
        return false;
    }

    std::string header;

    std::getline(input_, header);
    return true;
}

bool CsvLobReader::next(BookEvent& event) {
    std::string line;
    while (std::getline(input_, line)) {
        if (line.empty()) {
            continue;
        }
        if (parse_lob_line(line, event)) {
            ++rows_read_;
            return true;
        }
    }
    return false;
}

CsvTradeReader::CsvTradeReader(std::string path) : path_(std::move(path)) {}

bool CsvTradeReader::open() {
    input_.close();
    input_.clear();
    input_.open(path_);
    rows_read_ = 0;
    if (!input_.is_open()) {
        return false;
    }

    std::string header;

    std::getline(input_, header);
    return true;
}

bool CsvTradeReader::next(TradeEvent& event) {
    std::string line;
    while (std::getline(input_, line)) {
        if (line.empty()) {
            continue;
        }
        if (parse_trade_line(line, event)) {
            ++rows_read_;
            return true;
        }
    }
    return false;
}

}
