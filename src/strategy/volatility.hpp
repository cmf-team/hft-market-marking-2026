#pragma once

#include "common/BasicTypes.hpp"

#include <deque>

namespace cmf
{

class Volatility
{
  public:
    Volatility(int window, double dt);

    double update(Price prev_mid, Price curr_mid);

    double sigma() const { return compute_sigma(); }
    bool is_ready() const noexcept { return count_ >= 2; }
    int count() const noexcept { return count_; }

  private:
    void add_return(double log_ret);
    void remove_return(double log_ret);
    double compute_sigma() const;

    int window_;
    double dt_;
    std::deque<double> returns_;
    double mean_{0.0};
    double M2_{0.0};
    int count_{0};
};

} // namespace cmf
