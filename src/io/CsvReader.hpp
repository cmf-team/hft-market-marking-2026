#pragma once

#include "engine/Engine.hpp"
#include <fstream>
#include <string>
#include <vector>

namespace cmf
{

class CsvL2Reader : public IL2Reader
{
public:
    explicit CsvL2Reader(const std::string& path);
    bool next(L2Snapshot& out) override;

private:
    std::ifstream file_;
    int depth_{0};
    std::vector<int> askIdxPrice_, askIdxAmount_, bidIdxPrice_, bidIdxAmount_;
};

class CsvTradesReader : public ITradesReader
{
public:
    explicit CsvTradesReader(const std::string& path);
    bool next(TradeEvent& out) override;

private:
    std::ifstream file_;
};

} // namespace cmf
