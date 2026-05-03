#pragma once

#include <cstddef>
#include <fstream>
#include <string>

#include "types.hpp"

namespace hft {


class CsvLobReader {
   public:
    explicit CsvLobReader(std::string path);


    bool open();

    bool next(BookEvent& event);

    std::size_t rows_read() const { return rows_read_; }

   private:
    std::string path_;
    std::ifstream input_;
    std::size_t rows_read_ = 0;
};


class CsvTradeReader {
   public:
    explicit CsvTradeReader(std::string path);

    bool open();
    bool next(TradeEvent& event);

    std::size_t rows_read() const { return rows_read_; }

   private:
    std::string path_;
    std::ifstream input_;
    std::size_t rows_read_ = 0;
};

}
