#include "data_loader/csv_trades_queue.hpp"
#include "data_loader/csv_parsers.hpp"
#include "logging.hpp"

#include <stdexcept>

namespace hft::data {

CsvTradesQueue::CsvTradesQueue(const std::string &path) {
  if (path.empty()) {
    ended_ = true;
    return;
  }
  stream_.open(path);
  if (!stream_) {
    throw std::runtime_error("Cannot open trades file: " + path);
  }
  std::string header;
  std::getline(stream_, header);
}

QueueEvent<TradeData> CsvTradesQueue::tryPop() {
  if (ended_) {
    return EndOfStream{};
  }

  std::string line;
  while (std::getline(stream_, line)) {
    TradeData out;
    if (parseTradeLine(line, out)) {
      return out;
    }
  }

  ended_ = true;
  logging::Logger::debug("[CSV] trades queue closed");
  return EndOfStream{};
}

}
