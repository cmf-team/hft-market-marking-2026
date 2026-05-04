#include "parser.hpp"

#include <cassert>
#include <charconv>
#include <string>

MmapFile::MmapFile(std::string_view path)
{
    const int fd = ::open(path.data(), O_RDONLY);
    if (fd == -1)
        throw std::runtime_error("open failed: " + std::string(path));

    struct stat st{};
    if (::fstat(fd, &st) == -1)
    {
        ::close(fd);
        throw std::runtime_error("fstat failed");
    }

    size = static_cast<size_t>(st.st_size);
    void* ptr = ::mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    if (ptr == MAP_FAILED)
        throw std::runtime_error("mmap failed");

    ::madvise(ptr, size, MADV_SEQUENTIAL);
    data = static_cast<const char*>(ptr);
}

MmapFile::~MmapFile()
{
    if (data)
        ::munmap(const_cast<char*>(data), size);
}

MmapFile::MmapFile(MmapFile&& o) noexcept : data(o.data), size(o.size)
{
    o.data = nullptr;
    o.size = 0;
}

MmapFile& MmapFile::operator=(MmapFile&& o) noexcept
{
    if (this != &o)
    {
        if (data)
            ::munmap(const_cast<char*>(data), size);
        data = o.data;
        size = o.size;
        o.data = nullptr;
        o.size = 0;
    }
    return *this;
}

const char* skip_to(const char* p, const char* end, char delim) noexcept
{
    while (p < end && *p != delim) [[likely]]
        ++p;
    return (p < end) ? p + 1 : end;
}

const char* skip_line(const char* p, const char* end) noexcept
{
    while (p < end && *p != '\n')
        ++p;
    if (p < end)
        ++p;
    return p;
}

const char* parse_u64(const char* p, const char* end, uint64_t& out) noexcept
{
    auto [ptr, ec] = std::from_chars(p, end, out);
    assert(ec == std::errc{} && "parse_u64: from_chars failed");
    return ptr;
}

const char* parse_price(const char* p, const char* end, int64_t& out) noexcept
{
    uint64_t int_part = 0;
    auto [p2, ec1] = std::from_chars(p, end, int_part);
    assert(ec1 == std::errc{} && "parse_price: from_chars failed on integer part");
    if (p2 >= end || *p2 != '.')
    {
        out = static_cast<int64_t>(int_part * static_cast<uint64_t>(PRICE_SCALE));
        return p2;
    }
    ++p2;
    uint64_t frac = 0;
    int digits = 0;
    while (p2 < end && *p2 >= '0' && *p2 <= '9' && digits < PRICE_SCALE_DIG)
    {
        frac = frac * 10 + static_cast<unsigned>(*p2 - '0');
        ++digits;
        ++p2;
    }
    while (p2 < end && *p2 >= '0' && *p2 <= '9')
        ++p2;
    while (digits++ < PRICE_SCALE_DIG)
        frac *= 10;
    out = static_cast<int64_t>(int_part * static_cast<uint64_t>(PRICE_SCALE) + frac);
    return p2;
}

const char* parse_amount(const char* p, const char* end, int64_t& out) noexcept
{
    uint64_t v = 0;
    auto [p2, ec] = std::from_chars(p, end, v);
    assert(ec == std::errc{} && "parse_amount: from_chars failed");
    out = static_cast<int64_t>(v);
    if (p2 < end && *p2 == '.')
    {
        ++p2;
        while (p2 < end && *p2 >= '0' && *p2 <= '9')
            ++p2;
    }
    return p2;
}

const char* parse_lob_row(const char* p, const char* end, LobSnapshot& out) noexcept
{
    p = skip_to(p, end, ',');
    p = parse_u64(p, end, out.timestamp);
    for (int k = 0; k < LOB_DEPTH; ++k)
    {
        p = skip_to(p, end, ',');
        p = parse_price(p, end, out.asks[k].price);
        p = skip_to(p, end, ',');
        p = parse_amount(p, end, out.asks[k].amount);
        p = skip_to(p, end, ',');
        p = parse_price(p, end, out.bids[k].price);
        p = skip_to(p, end, ',');
        p = parse_amount(p, end, out.bids[k].amount);
    }
    return skip_line(p, end);
}

const char* parse_trade_row(const char* p, const char* end, TradeEvent& out) noexcept
{
    p = skip_to(p, end, ',');
    p = parse_u64(p, end, out.timestamp);
    p = skip_to(p, end, ',');
    out.side = (*p == 'b') ? Side::Buy : Side::Sell;
    p = skip_to(p, end, ',');
    p = parse_price(p, end, out.price);
    p = skip_to(p, end, ',');
    p = parse_amount(p, end, out.amount);
    return skip_line(p, end);
}

LobReader::LobReader(const MmapFile& f) : cur(f.data), end(f.data + f.size)
{
    cur = skip_line(cur, end);
}

bool LobReader::empty() const noexcept
{
    return !valid && cur >= end;
}

bool LobReader::peek(LobSnapshot& out) noexcept
{
    if (!valid && cur < end)
    {
        cur = parse_lob_row(cur, end, peeked);
        valid = true;
    }
    if (valid)
    {
        out = peeked;
        return true;
    }
    return false;
}

void LobReader::advance() noexcept
{
    valid = false;
}

uint64_t LobReader::peek_ts() noexcept
{
    if (!valid && cur < end)
    {
        cur = parse_lob_row(cur, end, peeked);
        valid = true;
    }
    return valid ? peeked.timestamp : std::numeric_limits<std::uint64_t>::max();
}

TradeReader::TradeReader(const MmapFile& f) : cur(f.data), end(f.data + f.size)
{
    cur = skip_line(cur, end);
}

bool TradeReader::empty() const noexcept
{
    return !valid && cur >= end;
}

bool TradeReader::peek(TradeEvent& out) noexcept
{
    if (!valid && cur < end)
    {
        cur = parse_trade_row(cur, end, peeked);
        valid = true;
    }
    if (valid)
    {
        out = peeked;
        return true;
    }
    return false;
}

void TradeReader::advance() noexcept
{
    valid = false;
}

uint64_t TradeReader::peek_ts() noexcept
{
    if (!valid && cur < end)
    {
        cur = parse_trade_row(cur, end, peeked);
        valid = true;
    }
    return valid ? peeked.timestamp : std::numeric_limits<std::uint64_t>::max();
}

bool validate_lob_file(const MmapFile& f, size_t sample_rows) noexcept
{
    const char* p = f.data;
    const char* end = f.data + f.size;
    p = skip_line(p, end);
    for (size_t i = 0; i < sample_rows && p < end; ++i)
    {
        LobSnapshot snap{};
        p = parse_lob_row(p, end, snap);
        if (snap.timestamp == 0 || snap.asks[0].price <= 0 || snap.bids[0].price <= 0)
            return false;
        if (snap.asks[0].price <= snap.bids[0].price)
            return false;
    }
    return true;
}
