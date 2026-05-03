#pragma once
#include <cstdint>
#include <array>
#include <variant>


namespace data {
// TODO to env or config
constexpr size_t BOOK_DEPTH = 25;

struct PriceLevel {
    uint64_t price;   // scaled double
    uint64_t amount;  // scaled double

    PriceLevel() = default;

    PriceLevel(double p, double a)
        : price(static_cast<uint64_t>(p * 1e9)),
          amount(static_cast<uint64_t>(a * 1e9)) {}
};

enum class Side : uint8_t {
    Buy,
    Sell
};

struct Trade {
    uint64_t local_timestamp;
    Side side;
    PriceLevel px_qty;
};

struct OrderBookSnapshot {
    uint64_t local_timestamp;

    std::array<PriceLevel, BOOK_DEPTH> asks;
    std::array<PriceLevel, BOOK_DEPTH> bids;
};

enum class EventType {
    Trade,
    Snapshot
};

struct Event {
    uint64_t local_timestamp;
    EventType type;

    std::variant<Trade, OrderBookSnapshot> data;

    bool operator<(const Event& other) const {
        return local_timestamp < other.local_timestamp;
    }
};

}