#pragma once

#include <string>

namespace hft::data {

/**
 * @brief Stores runtime inputs accepted from the command line.
 */
struct RunConfig {
  std::string lob_path;
  std::string trades_path;
};

/**
 * @brief Parses command line arguments into runtime configuration.
 * @param argc Number of command line arguments.
 * @param argv Command line argument values.
 * @return Parsed run configuration.
 */
RunConfig parseArgs(int argc, const char * const *argv);

}
