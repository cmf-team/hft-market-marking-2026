#include "strategy/volatility.hpp"

#include <cmath>

namespace cmf
{

Volatility::Volatility(int window, double dt)
    : window_(window), dt_(dt) {}

double Volatility::update(Price prev_mid, Price curr_mid)
{
    const double log_ret = std::log(curr_mid / prev_mid);

    if (static_cast<int>(returns_.size()) == window_)
    {
        const double old_val = returns_.front();
        returns_.pop_front();
        remove_return(old_val);
    }

    returns_.push_back(log_ret);
    add_return(log_ret);

    return compute_sigma();
}

void Volatility::add_return(double log_ret)
{
    ++count_;
    const double delta = log_ret - mean_;
    mean_ += delta / count_;
    const double delta2 = log_ret - mean_;
    M2_ += delta * delta2;
}

void Volatility::remove_return(double log_ret)
{
    --count_;
    const double delta = log_ret - mean_;
    mean_ -= delta / count_;
    const double delta2 = log_ret - mean_;
    M2_ -= delta * delta2;

    if (M2_ < 0.0)
        M2_ = 0.0;
}

double Volatility::compute_sigma() const
{
    if (count_ < 2)
        return 0.0;

    double variance = M2_ / (count_ - 1);
    if (variance < 0.0)
        variance = 0.0;

    return std::sqrt(variance / dt_);
}

} // namespace cmf
