#include "data_loader/csv_lob_queue.hpp"
#include "data_loader/csv_parsers.hpp"
#include "logging.hpp"

#include <stdexcept>

namespace hft::data {

CsvLobQueue::CsvLobQueue(const std::string &path, int maxDepth)
    : max_depth_(maxDepth) {
  if (path.empty()) {
    ended_ = true;
    return;
  }
  stream_.open(path);
  if (!stream_) {
    throw std::runtime_error("Cannot open LOB file: " + path);
  }
  std::string header;
  std::getline(stream_, header);
}

QueueEvent<LOBData> CsvLobQueue::tryPop() {
  if (ended_) {
    return EndOfStream{};
  }

  std::string line;
  while (std::getline(stream_, line)) {
    LOBData out;
    if (parseLobLine(line, max_depth_, out)) {
      return out;
    }
  }

  ended_ = true;
  logging::Logger::debug("[CSV] LOB queue closed");
  return EndOfStream{};
}

}
