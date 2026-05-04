#pragma once
#include "common/BasicTypes.hpp"

#include <cassert>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <string_view>

// POSIX / macOS
#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#error "MmapFile not implemented for Windows — use std::ifstream fallback"
#endif

// ---------------------------------------------------------------------------
// MmapFile — RAII wrapper for memory-mapped read-only file
// ---------------------------------------------------------------------------
struct MmapFile
{
    const char* data = nullptr;
    size_t size = 0;

    explicit MmapFile(std::string_view path);
    ~MmapFile();

    MmapFile(const MmapFile&) = delete;
    MmapFile& operator=(const MmapFile&) = delete;
    MmapFile(MmapFile&& o) noexcept;
    MmapFile& operator=(MmapFile&& o) noexcept;
};

// ---------------------------------------------------------------------------
// Low-level parsing helpers — zero-copy, no allocation
// ---------------------------------------------------------------------------

[[nodiscard]] const char* skip_to(const char* p, const char* end, char delim) noexcept;
[[nodiscard]] const char* skip_line(const char* p, const char* end) noexcept;
[[nodiscard]] const char* parse_u64(const char* p, const char* end, uint64_t& out) noexcept;
[[nodiscard]] const char* parse_price(const char* p, const char* end, int64_t& out) noexcept;
[[nodiscard]] const char* parse_amount(const char* p, const char* end, int64_t& out) noexcept;

// ---------------------------------------------------------------------------
// Row parsers
// ---------------------------------------------------------------------------

[[nodiscard]] const char* parse_lob_row(const char* p, const char* end, LobSnapshot& out) noexcept;
[[nodiscard]] const char* parse_trade_row(const char* p, const char* end, TradeEvent& out) noexcept;

// ---------------------------------------------------------------------------
// Peek-advance readers — hold one pre-parsed row as lookahead
// ---------------------------------------------------------------------------

struct LobReader
{
    const char* cur;
    const char* end;
    LobSnapshot peeked{};
    bool valid = false;

    explicit LobReader(const MmapFile& f);
    [[nodiscard]] bool empty() const noexcept;
    bool peek(LobSnapshot& out) noexcept;
    void advance() noexcept;
    [[nodiscard]] uint64_t peek_ts() noexcept;
};

struct TradeReader
{
    const char* cur;
    const char* end;
    TradeEvent peeked{};
    bool valid = false;

    explicit TradeReader(const MmapFile& f);
    [[nodiscard]] bool empty() const noexcept;
    bool peek(TradeEvent& out) noexcept;
    void advance() noexcept;
    [[nodiscard]] uint64_t peek_ts() noexcept;
};

[[nodiscard]] bool validate_lob_file(const MmapFile& f, size_t sample_rows = 5) noexcept;
