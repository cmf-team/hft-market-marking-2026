#include "common/CsvReader.hpp"

#include <cstdlib>
#include <stdexcept>

namespace cmf
{

namespace
{

const char* nextField(const char*& pos)
{
    const char* start = pos;
    while (*pos != '\0' && *pos != ',')
        ++pos;
    if (*pos == ',')
        ++pos;
    return start;
}

} // namespace

TradeReader::TradeReader(const std::string& path) : path_(path)
{
    file_.open(path);
    if (!file_.is_open())
        throw std::runtime_error("Cannot open trades file: " + path);
    std::getline(file_, line_); // skip header
}

bool TradeReader::next(Trade& trade)
{
    if (!std::getline(file_, line_))
        return false;

    const char* pos = line_.c_str();

    nextField(pos);
    const char* ts = nextField(pos);
    trade.timestamp = std::strtoll(ts, nullptr, 10);

    const char* side = nextField(pos);
    trade.side = (*side == 'b') ? Side::Buy : Side::Sell;

    const char* price = nextField(pos);
    trade.price = std::strtod(price, nullptr);

    trade.amount = std::strtod(pos, nullptr);
    return true;
}

void TradeReader::reset()
{
    file_.clear();
    file_.seekg(0);
    std::getline(file_, line_);
}

LOBReader::LOBReader(const std::string& path) : path_(path)
{
    file_.open(path);
    if (!file_.is_open())
        throw std::runtime_error("Cannot open LOB file: " + path);
    std::getline(file_, line_); // skip header
}

bool LOBReader::next(LOBSnapshot& snap)
{
    if (!std::getline(file_, line_))
        return false;

    const char* pos = line_.c_str();

    nextField(pos);
    const char* ts = nextField(pos);
    snap.timestamp = std::strtoll(ts, nullptr, 10);

    for (int i = 0; i < kLobDepth; ++i)
    {
        const char* ap = nextField(pos);
        snap.asks[i].price = std::strtod(ap, nullptr);

        const char* aa = nextField(pos);
        snap.asks[i].amount = std::strtod(aa, nullptr);

        const char* bp = nextField(pos);
        snap.bids[i].price = std::strtod(bp, nullptr);

        if (i < kLobDepth - 1)
        {
            const char* ba = nextField(pos);
            snap.bids[i].amount = std::strtod(ba, nullptr);
        }
        else
        {
            snap.bids[i].amount = std::strtod(pos, nullptr); // last field
        }
    }
    return true;
}

void LOBReader::reset()
{
    file_.clear();
    file_.seekg(0);
    std::getline(file_, line_);
}

} // namespace cmf
