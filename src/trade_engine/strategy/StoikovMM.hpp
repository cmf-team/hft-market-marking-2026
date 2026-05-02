#pragma once
#include "StrategyBase.hpp"

#include <cmath>
#include <vector>

class StoikovMM : public StrategyBase {
    double gamma_;
    double sigma_;
    double k_;
    double T_;
    double orderSize_;
    bool   useMicroprice_;
    double inventory_ = 0.0;
    long long nextId_ = 1;
    std::vector<long long> activeIds_;

public:
    StoikovMM(double gamma = 0.1, double sigma = 0.001,
              double k = 10000.0, double T = 1.0,
              double orderSize = 100.0, bool useMicroprice = true)
        : gamma_(gamma), sigma_(sigma), k_(k), T_(T),
          orderSize_(orderSize), useMicroprice_(useMicroprice) {}

    std::vector<Order> reactToLob(const OrderBookRow& lob) override {
        double mid = (lob.asks[0].price + lob.bids[0].price) / 2.0;

        // Stoikov (2018): microprice weights mid by opposite-side queue depth
        double fair = mid;
        if (useMicroprice_) {
            double bidQ = lob.bids[0].amount, askQ = lob.asks[0].amount;
            fair = (lob.asks[0].price * bidQ + lob.bids[0].price * askQ) / (bidQ + askQ);
        }
        updateMid(fair);

        // Avellaneda-Stoikov (2008)
        // reservation price: shift fair value away from accumulated inventory
        double r = fair - inventory_ * gamma_ * sigma_ * sigma_ * T_;

        // optimal spread
        double delta = gamma_ * sigma_ * sigma_ * T_
                     + (2.0 / gamma_) * std::log(1.0 + gamma_ / k_);

        std::vector<Order> orders;

        for (long long id : activeIds_)
            orders.push_back({id, lob.local_timestamp, 0, 0, 0, "", "cancel"});
        activeIds_.clear();

        double bidPrice = r - delta / 2.0;
        double askPrice = r + delta / 2.0;

        if (bidPrice > 0.0) {
            Order bid{nextId_++, lob.local_timestamp, bidPrice, orderSize_, 0.0, "buy", "new"};
            orders.push_back(bid);
            activeIds_.push_back(bid.id);
        }
        if (askPrice > 0.0) {
            Order ask{nextId_++, lob.local_timestamp, askPrice, orderSize_, 0.0, "sell", "new"};
            orders.push_back(ask);
            activeIds_.push_back(ask.id);
        }

        return orders;
    }

    void reactToExecution(const std::vector<Order>& fills) override {
        for (const Order& fill : fills)
            inventory_ += (fill.side == "buy" ? 1.0 : -1.0) * fill.amount;
        StrategyBase::reactToExecution(fills);
    }
};
