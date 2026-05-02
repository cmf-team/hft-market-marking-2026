#include "metrics/predefined_metrics.hpp"
#include "portfolio/portfolio.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace hft::metrics::predefined {

namespace {

constexpr std::size_t MIN_EQUITY_POINTS_FOR_SHARPE = 2;
constexpr double SHARPE_STDDEV_FLOOR = 1e-15;

}

double RealizedPnL::calculate(const portfolio::Portfolio &portfolio) const {
  return portfolio.realizedPnL();
}

double TotalPnL::calculate(const portfolio::Portfolio &portfolio) const {
  const auto &curve = portfolio.equityCurve();
  if (curve.empty()) {
    return portfolio.realizedPnL();
  }
  return curve.back() - portfolio.initialCash();
}

double Inventory::calculate(const portfolio::Portfolio &portfolio) const {
  return portfolio.inventory();
}

double Turnover::calculate(const portfolio::Portfolio &portfolio) const {
  return portfolio.turnover();
}

double FillCount::calculate(const portfolio::Portfolio &portfolio) const {
  return static_cast<double>(portfolio.fills().size());
}

double MaxDrawdown::calculate(const portfolio::Portfolio &portfolio) const {
  const auto &equity_curve = portfolio.equityCurve();
  if (equity_curve.empty()) {
    return 0.0;
  }

  double peak = equity_curve.front();
  double max_dd = 0.0;
  for (double equity : equity_curve) {
    if (equity > peak) {
      peak = equity;
    }
    if (peak > 0.0) {
      const double drawdown = (peak - equity) / peak;
      if (drawdown > max_dd) {
        max_dd = drawdown;
      }
    }
  }
  return max_dd;
}

double SharpeRatio::calculate(const portfolio::Portfolio &portfolio) const {
  const auto &equity_curve = portfolio.equityCurve();
  if (equity_curve.size() < MIN_EQUITY_POINTS_FOR_SHARPE) {
    return 0.0;
  }

  std::vector<double> returns;
  returns.reserve(equity_curve.size() - 1);
  for (std::size_t i = 1; i < equity_curve.size(); ++i) {
    if (equity_curve[i - 1] == 0.0) {
      continue;
    }
    returns.push_back((equity_curve[i] - equity_curve[i - 1]) /
                      equity_curve[i - 1]);
  }
  if (returns.size() < MIN_EQUITY_POINTS_FOR_SHARPE) {
    return 0.0;
  }

  const double mean = std::accumulate(returns.begin(), returns.end(), 0.0) /
                      static_cast<double>(returns.size());
  double var = 0.0;
  for (double r : returns)
    var += (r - mean) * (r - mean);
  var /= static_cast<double>(returns.size());
  const double sd = std::sqrt(var);
  if (sd < SHARPE_STDDEV_FLOOR) {
    return 0.0;
  }
  return mean / sd;
}

}
