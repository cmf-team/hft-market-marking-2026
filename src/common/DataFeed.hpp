#pragma once

#include "common/CsvReader.hpp"
#include "common/Types.hpp"

#include <cstdint>
#include <string>

namespace cmf
{

class DataFeed
{
  public:
    DataFeed(const std::string& trades_path, const std::string& lob_path);

    // Returns the next event (trade or LOB update) in timestamp order.
    // Fills the corresponding struct based on event_type.
    // Returns false when both streams are exhausted.
    bool next(EventType& event_type, Trade& trade, LOBSnapshot& lob);

    std::uint64_t eventsProcessed() const { return events_processed_; }

  private:
    TradeReader trade_reader_;
    LOBReader lob_reader_;

    Trade pending_trade_{};
    LOBSnapshot pending_lob_{};
    bool has_pending_trade_ = false;
    bool has_pending_lob_ = false;
    bool trade_eof_ = false;
    bool lob_eof_ = false;
    std::uint64_t events_processed_ = 0;

    void fetchTrade();
    void fetchLob();
};

} // namespace cmf
