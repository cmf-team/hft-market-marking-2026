#pragma once

// Internal CSV parsing helpers shared by the loaders. Not part of the public
// API; do not include from headers under include/bt/.

#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace bt::detail {

// Lightweight, allocation-free comma-delimited field iterator over a single
// line. Empty trailing fields are returned as empty string_views.
class FieldIter {
public:
    explicit FieldIter(const std::string_view line) noexcept : line_(line) {}

    // Returns the next field. Once `done()` is true, returns empty.
    std::string_view next() noexcept {
        if (pos_ > line_.size()) return {};
        const auto end = line_.find(',', pos_);
        const auto stop = (end == std::string_view::npos) ? line_.size() : end;
        std::string_view field = line_.substr(pos_, stop - pos_);
        pos_ = (end == std::string_view::npos) ? line_.size() + 1 : end + 1;
        return field;
    }

    [[nodiscard]] bool done() const noexcept { return pos_ > line_.size(); }

private:
    std::string_view line_;
    std::size_t      pos_ = 0;
};

[[noreturn]] inline void throw_parse_error(const std::string& path,
                                           std::size_t row,
                                           const std::string& what) {
    throw std::runtime_error(path + ": row " + std::to_string(row) + ": " + what);
}

inline std::int64_t parse_i64(std::string_view sv,
                              const std::string& path,
                              std::size_t row,
                              std::string_view field_name) {
    std::int64_t v = 0;
    const auto first = sv.data();
    const auto last  = sv.data() + sv.size();
    const auto [ptr, ec] = std::from_chars(first, last, v);
    if (ec != std::errc{} || ptr != last) {
        throw_parse_error(path, row,
                          "bad " + std::string(field_name) + " '" + std::string(sv) + "'");
    }
    return v;
}

inline double parse_double(std::string_view sv,
                           const std::string& path,
                           std::size_t row,
                           std::string_view field_name) {
    // We can't use std::from_chars for double here: macOS libc++ ships only
    // the integer overloads and explicitly deletes the bool one (which
    // ambiguously wins on a `double&` argument), so the call fails to
    // compile on Apple's stdlib. strtod is on every C++ stdlib, has the
    // exact error-reporting semantics we need, and the per-row cost is
    // negligible for a CSV loader that's already I/O bound.
    std::string buf(sv);
    const char* cstr = buf.c_str();
    char*       end_ptr = nullptr;
    errno = 0;
    const double v = std::strtod(cstr, &end_ptr);
    const bool consumed_all = (end_ptr != cstr) &&
        (static_cast<std::size_t>(end_ptr - cstr) == buf.size());
    if (!consumed_all || errno == ERANGE) {
        throw_parse_error(path, row,
                          "bad " + std::string(field_name) + " '" + std::string(sv) + "'");
    }
    return v;
}

}  // namespace bt::detail
