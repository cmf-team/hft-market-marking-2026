#pragma once
#include "BacktestEngine.hpp"
#include <cmath>

namespace cmf
{

class BaseStrategy
{
  protected:
    BacktestEngine* engine_;
    double gamma_;
    double sigma_;
    double k_;
    double T_t_;
    Quantity order_qty_ = 10000.0;

  public:
    BaseStrategy(BacktestEngine* engine, double gamma = 0.1, double sigma = 0.00001, double k = 20000000.0)
        : engine_(engine), gamma_(gamma), sigma_(sigma), k_(k), T_t_(1.0) {}

    virtual ~BaseStrategy() = default;
    virtual void updateQuotes() = 0;

    void placeQuotes(double reservation_price)
    {
        engine_->clearOrders();

        double spread = gamma_ * sigma_ * sigma_ * T_t_ + (2.0 / gamma_) * std::log(1.0 + gamma_ / k_);

        Price bid_px = reservation_price - spread / 2.0;
        Price ask_px = reservation_price + spread / 2.0;

        engine_->placeOrder(Side::Buy, bid_px, order_qty_);
        engine_->placeOrder(Side::Sell, ask_px, order_qty_);
    }
};

class StrategyAS2008 : public BaseStrategy
{
  public:
    using BaseStrategy::BaseStrategy;

    void updateQuotes() override
    {
        Price bid = engine_->getBestBid();
        Price ask = engine_->getBestAsk();
        if (bid == 0 || ask == 0)
            return;

        double s = (bid + ask) / 2.0;
        double q = engine_->getInventory() / order_qty_;

        double r = s - q * gamma_ * sigma_ * sigma_ * T_t_;
        placeQuotes(r);
    }
};

class StrategyMicroprice : public BaseStrategy
{
  public:
    using BaseStrategy::BaseStrategy;

    void updateQuotes() override
    {
        Price bid = engine_->getBestBid();
        Price ask = engine_->getBestAsk();
        Quantity bid_sz = engine_->getBidSize();
        Quantity ask_sz = engine_->getAskSize();

        if (bid == 0 || ask == 0 || (bid_sz + ask_sz) == 0)
            return;

        double I = bid_sz / (bid_sz + ask_sz);
        double micro_price = ask * I + bid * (1.0 - I);

        double q = engine_->getInventory() / order_qty_;
        double r = micro_price - q * gamma_ * sigma_ * sigma_ * T_t_;

        placeQuotes(r);
    }
};

} // namespace cmf