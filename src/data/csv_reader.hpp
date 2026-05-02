#pragma once

#include "book/order_book.hpp"
#include "core/event.hpp"

#include <fstream>
#include <string>

namespace cmf {

class LobCsvReader {
public:
    explicit LobCsvReader(const std::string& path);

    bool next(OrderBook& book);

    bool good() const noexcept { return static_cast<bool>(file_); }
    bool eof()  const noexcept { return file_.eof(); }

    std::size_t rows_read() const noexcept { return rows_read_; }

private:
    std::ifstream file_;
    std::string   line_;
    std::string   header_;
    std::size_t   rows_read_{0};
};

class TradesCsvReader {
public:
    explicit TradesCsvReader(const std::string& path);

    bool next(Trade& trade);

    bool good() const noexcept { return static_cast<bool>(file_); }
    bool eof()  const noexcept { return file_.eof(); }

    std::size_t rows_read() const noexcept { return rows_read_; }

private:
    std::ifstream file_;
    std::string   line_;
    std::string   header_;
    std::size_t   rows_read_{0};
};

}  // namespace cmf
