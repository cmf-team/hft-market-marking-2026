#pragma once

#include "data_loader/event_queue.hpp"
#include "types.hpp"

#include <fstream>
#include <string>

namespace hft::data {

/**
 * @brief Produces limit order book snapshots from a CSV stream.
 */
class CsvLobQueue {
public:
  using value_type = LOBData;

  static constexpr int DEFAULT_MAX_DEPTH = 5;

  /**
   * @brief Opens a CSV file-backed book snapshot queue.
   * @param path Input CSV file path.
   * @param maxDepth Maximum number of price levels to parse per side.
   */
  explicit CsvLobQueue(const std::string &path,
                       int maxDepth = DEFAULT_MAX_DEPTH);

  /**
   * @brief Disables copying of the file-backed queue.
   * @param other Queue that would be copied.
   */
  CsvLobQueue(const CsvLobQueue &other) = delete;

  /**
   * @brief Disables copy assignment of the file-backed queue.
   * @param other Queue that would be assigned from.
   * @return Reference to this queue.
   */
  CsvLobQueue &operator=(const CsvLobQueue &other) = delete;

  /**
   * @brief Moves a file-backed queue.
   * @param other Queue to move from.
   */
  CsvLobQueue(CsvLobQueue &&other) noexcept = default;

  /**
   * @brief Move-assigns a file-backed queue.
   * @param other Queue to move from.
   * @return Reference to this queue.
   */
  CsvLobQueue &operator=(CsvLobQueue &&other) noexcept = default;

  /**
   * @brief Reads the next book snapshot or end-of-stream marker.
   * @return Queue event containing a snapshot or EndOfStream.
   */
  QueueEvent<LOBData> tryPop();

private:
  std::ifstream stream_;
  int max_depth_{DEFAULT_MAX_DEPTH};
  bool ended_{false};
};

}
