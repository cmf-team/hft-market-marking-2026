#include "common/DataFeed.hpp"

namespace cmf
{

DataFeed::DataFeed(const std::string& trades_path, const std::string& lob_path)
    : trade_reader_(trades_path), lob_reader_(lob_path)
{
    fetchTrade();
    fetchLob();
}

void DataFeed::fetchTrade()
{
    if (!trade_eof_)
    {
        has_pending_trade_ = trade_reader_.next(pending_trade_);
        if (!has_pending_trade_)
            trade_eof_ = true;
    }
}

void DataFeed::fetchLob()
{
    if (!lob_eof_)
    {
        has_pending_lob_ = lob_reader_.next(pending_lob_);
        if (!has_pending_lob_)
            lob_eof_ = true;
    }
}

bool DataFeed::next(EventType& event_type, Trade& trade, LOBSnapshot& lob)
{
    if (!has_pending_trade_ && !has_pending_lob_)
        return false;

    bool use_trade;
    if (has_pending_trade_ && has_pending_lob_)
        use_trade = (pending_trade_.timestamp <= pending_lob_.timestamp);
    else
        use_trade = has_pending_trade_;

    if (use_trade)
    {
        event_type = EventType::Trade;
        trade = pending_trade_;
        fetchTrade();
    }
    else
    {
        event_type = EventType::LOBUpdate;
        lob = pending_lob_;
        fetchLob();
    }

    ++events_processed_;
    return true;
}

} // namespace cmf
