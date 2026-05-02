#include "execution/backtest_engine.hpp"

namespace hft::execution {

void BacktestEngine::setPortfolio(portfolio::Portfolio::SPtr p) {
  portfolio_ = std::move(p);
}

void BacktestEngine::setConfig(const BacktestConfig &cfg) { config_ = cfg; }

const portfolio::Portfolio &BacktestEngine::portfolio() const {
  return *portfolio_;
}

const MatchingEngine &BacktestEngine::matchingEngine() const {
  return matching_;
}

const BacktestConfig &BacktestEngine::config() const { return config_; }

}
