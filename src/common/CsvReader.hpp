#pragma once

#include "common/Types.hpp"

#include <fstream>
#include <string>

namespace cmf
{

class TradeReader
{
  public:
    explicit TradeReader(const std::string& path);
    bool next(Trade& trade);
    void reset();

  private:
    std::string path_;
    std::ifstream file_;
    std::string line_;
};

class LOBReader
{
  public:
    explicit LOBReader(const std::string& path);
    bool next(LOBSnapshot& snapshot);
    void reset();

  private:
    std::string path_;
    std::ifstream file_;
    std::string line_;
};

} // namespace cmf
