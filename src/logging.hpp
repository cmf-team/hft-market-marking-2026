#pragma once

#include <iostream>

namespace hft::logging {

#if defined(NO_LOGGING) && !defined(HFT_NO_LOGGING)
#define HFT_NO_LOGGING
#endif

/**
 * @brief Writes debug logs when logging is enabled.
 */
class Logger {
public:
  /**
   * @brief Writes debug arguments to standard output.
   * @param args Values streamed to the log line.
   */
  template <typename... Args>
  static void debug([[maybe_unused]] Args &&...args) {
#ifndef HFT_NO_LOGGING
    (std::cout << ... << args) << '\n';
#endif
  }
};

}
