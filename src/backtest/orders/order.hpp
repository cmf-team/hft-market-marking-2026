#pragma once

#include <cstdint>
#include <memory>

namespace backtest {

enum class Side : uint8_t {
    Buy,
    Sell
};

enum class OrderStatus : uint8_t {
    New,
    PartiallyFilled,
    Filled,
    Cancelled,
    Rejected
};

class Order {
public:
    Order(uint64_t id,
          uint64_t ts,
          Side side,
          uint64_t qty)
        : order_id_(id),
          timestamp_(ts),
          side_(side),
          quantity_(qty),
          filled_qty_(0),
          status_(OrderStatus::New) {}

    virtual ~Order() = default;

    uint64_t id() const noexcept { return order_id_; }
    uint64_t timestamp() const noexcept { return timestamp_; }
    Side side() const noexcept { return side_; }
    uint64_t quantity() const noexcept { return quantity_; }
    uint64_t filled_qty() const noexcept { return filled_qty_; }
    OrderStatus status() const noexcept { return status_; }

    uint64_t remaining() const noexcept {
        return quantity_ - filled_qty_;
    }

    void fill(uint64_t qty) noexcept {
        filled_qty_ += qty;
        if (filled_qty_ == quantity_) {
            status_ = OrderStatus::Filled;
        } else {
            status_ = OrderStatus::PartiallyFilled;
        }
    }

    void cancel() noexcept {
        status_ = OrderStatus::Cancelled;
    }

    virtual bool is_market() const noexcept = 0;

private:
    uint64_t order_id_;
    uint64_t timestamp_;
    Side side_;
    uint64_t quantity_;
    uint64_t filled_qty_;
    OrderStatus status_;
};

using OrderPtr = std::unique_ptr<Order>;

} // namespace backtest