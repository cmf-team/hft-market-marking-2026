#pragma once
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "data_ingestion/data_types.hpp"

namespace data::parser {

class TradeCSVParser {
public:
    Trade parse_line(const std::string& line) const {
        auto tokens = split(line);
        if (tokens.size() < 5)
            throw std::runtime_error("Trade line has too few columns");
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

    static Trade parse_row(const std::vector<std::string>& tokens) {
        Trade trade;
        trade.local_timestamp = std::stoull(tokens[1]);
        trade.side            = (tokens[2] == "buy") ? Side::Buy : Side::Sell;
        trade.px_qty          = PriceLevel(std::stod(tokens[3]), std::stod(tokens[4]));
        return trade;
    }
};

} // namespace data::parser
