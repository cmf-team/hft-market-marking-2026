#pragma once
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "data_ingestion/data_types.hpp"

namespace data::parser {

class LOBCSVParser {
public:
    OrderBookSnapshot parse_line(const std::string& line) const {
        auto tokens = split(line);
        if (tokens.size() < 2 + 4 * BOOK_DEPTH)
            throw std::runtime_error("LOB line has too few columns");
        return parse_row(tokens);
    }

private:
    static std::vector<std::string> split(const std::string& line) {
        std::vector<std::string> result;
        std::stringstream ss(line);
        std::string item;
        while (std::getline(ss, item, ','))
            result.push_back(item);
        return result;
    }

    static OrderBookSnapshot parse_row(const std::vector<std::string>& tokens) {
        OrderBookSnapshot snap;
        snap.local_timestamp = std::stoull(tokens[1]);
        size_t idx = 2;
        for (size_t i = 0; i < BOOK_DEPTH; ++i) {
            snap.asks[i] = PriceLevel(std::stod(tokens[idx]), std::stod(tokens[idx + 1]));
            idx += 2;
            snap.bids[i] = PriceLevel(std::stod(tokens[idx]), std::stod(tokens[idx + 1]));
            idx += 2;
        }
        return snap;
    }
};

} // namespace data::parser
