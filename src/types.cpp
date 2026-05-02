#include "types.hpp"

namespace hft {

Side oppositeSide(const Side side) noexcept {
  return side == Side::Buy ? Side::Sell : Side::Buy;
}

Timestamp eventTs(const MarketEvent &market_event) noexcept {
  return std::visit([](const auto &payload) { return payload.ts; }, market_event);
}

}
