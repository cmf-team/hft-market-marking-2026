#pragma once

#include "types.hpp"

#include <string>

namespace hft::data {

/**
 * @brief Parses one CSV row into a limit order book snapshot.
 * @param line Input CSV row.
 * @param maxDepth Maximum number of price levels to read per side.
 * @param[out] out Parsed snapshot when parsing succeeds.
 * @return True when the row is valid and was parsed.
 */
bool parseLobLine(const std::string &line, int maxDepth, LOBData &out);

/**
 * @brief Parses one CSV row into a trade print.
 * @param line Input CSV row.
 * @param[out] out Parsed trade when parsing succeeds.
 * @return True when the row is valid and was parsed.
 */
bool parseTradeLine(const std::string &line, TradeData &out);

}
