#pragma once

#include "data_loader/event_queue.hpp"
#include "types.hpp"

#include <concepts>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

namespace hft::data {

/**
 * @brief Merges time-ordered book and trade queues into one market event stream.
 */
template <EventQueue LobQ, EventQueue TradeQ>
  requires std::same_as<typename LobQ::value_type, LOBData> &&
           std::same_as<typename TradeQ::value_type, TradeData>
class StreamingEventSource {
public:
  /**
   * @brief Creates a streaming source over existing queues.
   * @param lob_queue Input book snapshot queue that must outlive this source.
   * @param trades_queue Input trade print queue that must outlive this source.
   */
  StreamingEventSource(LobQ &lob_queue, TradeQ &trades_queue) noexcept
      : lob_queue_(std::addressof(lob_queue)),
        trades_queue_(std::addressof(trades_queue)) {
    refillLob();
    refillTrade();
  }

  /**
   * @brief Disables copying of a non-owning streaming source.
   * @param other Source that would be copied.
   */
  StreamingEventSource(const StreamingEventSource &other) = delete;

  /**
   * @brief Disables copy assignment of a non-owning streaming source.
   * @param other Source that would be assigned from.
   * @return Reference to this source.
   */
  StreamingEventSource &operator=(const StreamingEventSource &other) = delete;

  /**
   * @brief Moves a streaming source and its cached events.
   * @param other Source to move from.
   */
  StreamingEventSource(StreamingEventSource &&other) noexcept = default;

  /**
   * @brief Disables move assignment of a non-owning streaming source.
   * @param other Source that would be assigned from.
   * @return Reference to this source.
   */
  StreamingEventSource &operator=(StreamingEventSource &&other) noexcept = delete;

  /**
   * @brief Checks whether any queued market event remains.
   * @return True when a book or trade event is cached.
   */
  bool hasNext() const noexcept {
    return peek_lob_.has_value() || peek_trade_.has_value();
  }

  /**
   * @brief Returns the next event in timestamp order.
   * @return Next market event from the book or trade queue.
   */
  MarketEvent next() {
    const bool takeLob =
        peek_lob_.has_value() &&
        (!peek_trade_.has_value() || peek_lob_->ts <= peek_trade_->ts);

    if (takeLob) {
      MarketEvent market_event{std::move(*peek_lob_)};
      peek_lob_.reset();
      refillLob();
      return market_event;
    }

    MarketEvent market_event{std::move(*peek_trade_)};
    peek_trade_.reset();
    refillTrade();
    return market_event;
  }

private:
  /**
   * @brief Refreshes the cached book snapshot from the book queue.
   */
  void refillLob() {
    auto event = lob_queue_->tryPop();
    if (auto *lob = std::get_if<LOBData>(&event)) {
      peek_lob_ = std::move(*lob);
      return;
    }

    peek_lob_.reset();
  }

  /**
   * @brief Refreshes the cached trade print from the trade queue.
   */
  void refillTrade() {
    auto event = trades_queue_->tryPop();
    if (auto *trade = std::get_if<TradeData>(&event)) {
      peek_trade_ = std::move(*trade);
      return;
    }

    peek_trade_.reset();
  }

  LobQ *lob_queue_;
  TradeQ *trades_queue_;
  std::optional<LOBData> peek_lob_;
  std::optional<TradeData> peek_trade_;
};

}
