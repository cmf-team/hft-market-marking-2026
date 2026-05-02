#include "data_loader/run_config.hpp"

#include <iostream>
#include <stdexcept>

namespace hft::data {

namespace {

inline std::string requireValue(int &i, int argc, const char * const *argv,
                                const std::string &arg) {
  if (i + 1 >= argc)
    throw std::runtime_error(arg + " requires a value");
  return argv[++i];
}

}

RunConfig parseArgs(int argc, const char * const *argv) {
  RunConfig cfg;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--lob") {
      cfg.lob_path = requireValue(i, argc, argv, arg);
    } else if (arg == "--trades") {
      cfg.trades_path = requireValue(i, argc, argv, arg);
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: " << argv[0]
                << " --lob <path> --trades <path>\n"
                << "  --lob       LOB snapshots CSV (required)\n"
                << "  --trades    trades CSV (required)\n"
                << "  --help\n";
      std::exit(0);
    } else {
      throw std::runtime_error("Unknown argument: " + arg);
    }
  }

  if (cfg.lob_path.empty())
    throw std::runtime_error("--lob is required");
  if (cfg.trades_path.empty())
    throw std::runtime_error("--trades is required");

  return cfg;
}

}
