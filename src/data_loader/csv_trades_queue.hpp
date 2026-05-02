#pragma once

#include "data_loader/event_queue.hpp"
#include "types.hpp"

#include <fstream>
#include <string>

namespace hft::data {

/**
 * @brief Produces trade prints from a CSV stream.
 */
class CsvTradesQueue {
public:
  using value_type = TradeData;

  /**
   * @brief Opens a CSV file-backed trade queue.
   * @param path Input CSV file path.
   */
  explicit CsvTradesQueue(const std::string &path);

  /**
   * @brief Disables copying of the file-backed queue.
   * @param other Queue that would be copied.
   */
  CsvTradesQueue(const CsvTradesQueue &other) = delete;

  /**
   * @brief Disables copy assignment of the file-backed queue.
   * @param other Queue that would be assigned from.
   * @return Reference to this queue.
   */
  CsvTradesQueue &operator=(const CsvTradesQueue &other) = delete;

  /**
   * @brief Moves a file-backed queue.
   * @param other Queue to move from.
   */
  CsvTradesQueue(CsvTradesQueue &&other) noexcept = default;

  /**
   * @brief Move-assigns a file-backed queue.
   * @param other Queue to move from.
   * @return Reference to this queue.
   */
  CsvTradesQueue &operator=(CsvTradesQueue &&other) noexcept = default;

  /**
   * @brief Reads the next trade print or end-of-stream marker.
   * @return Queue event containing a trade print or EndOfStream.
   */
  QueueEvent<TradeData> tryPop();

private:
  std::ifstream stream_;
  bool ended_{false};
};

}
