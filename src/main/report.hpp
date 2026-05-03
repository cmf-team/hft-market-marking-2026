#pragma once

#include <string>

#include "config.hpp"
#include "stats.hpp"

namespace hft {


std::string build_report_markdown(const BacktestConfig& config,
                                  const BacktestStats& stats,
                                  const std::string& strategy_name);


bool write_report(const std::string& output_path, const std::string& content);

}
