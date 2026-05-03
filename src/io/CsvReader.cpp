#include "io/CsvReader.hpp"
#include <sstream>

namespace cmf
{

static bool getline_stripped(std::ifstream& f, std::string& out)
{
    if (!std::getline(f, out))
        return false;
    while (!out.empty() && (out.back() == '\r' || out.back() == '\n'))
        out.pop_back();
    return true;
}

CsvL2Reader::CsvL2Reader(const std::string& path) : file_(path)
{
    std::string header;
    getline_stripped(file_, header);
    std::vector<std::string> cols;
    std::stringstream hs(header);
    std::string colname;
    while (std::getline(hs, colname, ','))
        cols.push_back(colname);

    for (std::size_t i = 0; i < cols.size(); ++i)
    {
        const std::string& col = cols[i];
        const bool isAsk = (col.rfind("asks[", 0) == 0);
        const bool isBid = (col.rfind("bids[", 0) == 0);
        if (!isAsk && !isBid)
            continue;
        const auto bracket = col.find(']');
        if (bracket == std::string::npos)
            continue;
        const int k = std::stoi(col.substr(5, bracket - 5));
        const bool isPrice = (col.find("price") != std::string::npos);
        const bool isAmount = (col.find("amount") != std::string::npos);
        const int idx = static_cast<int>(i);

        auto ensure = [](std::vector<int>& v, int sz) {
            if (static_cast<int>(v.size()) <= sz)
                v.resize(static_cast<std::size_t>(sz + 1), -1);
        };

        if (isAsk && isPrice)
        {
            ensure(askIdxPrice_, k);
            askIdxPrice_[static_cast<std::size_t>(k)] = idx;
        }
        else if (isAsk && isAmount)
        {
            ensure(askIdxAmount_, k);
            askIdxAmount_[static_cast<std::size_t>(k)] = idx;
        }
        else if (isBid && isPrice)
        {
            ensure(bidIdxPrice_, k);
            bidIdxPrice_[static_cast<std::size_t>(k)] = idx;
        }
        else if (isBid && isAmount)
        {
            ensure(bidIdxAmount_, k);
            bidIdxAmount_[static_cast<std::size_t>(k)] = idx;
        }
    }
    const int ap = static_cast<int>(askIdxPrice_.size());
    const int aa = static_cast<int>(askIdxAmount_.size());
    const int bp = static_cast<int>(bidIdxPrice_.size());
    const int ba = static_cast<int>(bidIdxAmount_.size());
    depth_ = std::min(std::min(ap, aa), std::min(bp, ba));
}

bool CsvL2Reader::next(L2Snapshot& out)
{
    std::string line;
    if (!getline_stripped(file_, line))
        return false;
    std::vector<std::string> cells;
    cells.reserve(static_cast<std::size_t>(2 + 4 * depth_));
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ','))
        cells.push_back(cell);

    out.ts = static_cast<NanoTime>(std::stoll(cells[1]));
    out.asks.clear();
    out.bids.clear();
    for (int k = 0; k < depth_; ++k)
    {
        const auto ks = static_cast<std::size_t>(k);
        const double ap = std::stod(cells[static_cast<std::size_t>(askIdxPrice_[ks])]);
        const double aa = std::stod(cells[static_cast<std::size_t>(askIdxAmount_[ks])]);
        const double bp = std::stod(cells[static_cast<std::size_t>(bidIdxPrice_[ks])]);
        const double ba = std::stod(cells[static_cast<std::size_t>(bidIdxAmount_[ks])]);
        out.asks.push_back({ap, aa});
        out.bids.push_back({bp, ba});
    }
    return true;
}

CsvTradesReader::CsvTradesReader(const std::string& path) : file_(path)
{
    std::string header;
    getline_stripped(file_, header);
}

bool CsvTradesReader::next(TradeEvent& out)
{
    std::string line;
    if (!getline_stripped(file_, line))
        return false;
    std::stringstream ss(line);
    std::string cell;
    std::getline(ss, cell, ','); // index
    std::getline(ss, cell, ',');
    out.ts = static_cast<NanoTime>(std::stoll(cell));
    std::getline(ss, cell, ',');
    out.side = (cell == "buy") ? Side::Buy : Side::Sell;
    std::getline(ss, cell, ',');
    out.price = std::stod(cell);
    std::getline(ss, cell, ',');
    out.amount = std::stod(cell);
    return true;
}

} // namespace cmf
