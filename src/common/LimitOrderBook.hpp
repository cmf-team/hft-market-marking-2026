#pragma once
#include "EventTypes.hpp"
#include <map>
#include <cmath>

namespace cmf {

class LimitOrderBook {
public:
    void applyUpdate(const LOBEvent& ev) {
        auto& levels = (ev.side == Side::Buy) ? bids_ : asks_;
        if (ev.qty <= 0.0)
            levels.erase(ev.price);
        else
            levels[ev.price] = ev.qty;
    }

    Price bestBid() const {
        if (bids_.empty()) return 0;
        // bids_ is ascending, so the highest price is the last element
        return bids_.rbegin()->first;
    }
    Price bestAsk() const {
        if (asks_.empty()) return 0;
        return asks_.begin()->first;
    }
    Price midPrice() const {
        return (bestBid() + bestAsk()) / 2.0;
    }

    Price microPrice(double weight = 0.3) const {
        if (bids_.empty() || asks_.empty()) return midPrice();
        double bidVol = totalVolume(bids_, 3);
        double askVol = totalVolume(asks_, 3);
        double halfSpread = (bestAsk() - bestBid()) / 2.0;
        double imbalance = (bidVol - askVol) / (bidVol + askVol);
        return midPrice() + weight * imbalance * halfSpread;
    }

    bool wouldBuyFill(Price orderPrice) const {
        if (asks_.empty()) return false;
        return orderPrice >= bestAsk();
    }
    bool wouldSellFill(Price orderPrice) const {
        if (bids_.empty()) return false;
        return orderPrice <= bestBid();
    }

private:
    // Both maps now use the default std::less → ascending order.
    // bestAsk = begin(), bestBid = rbegin()
    std::map<Price, Quantity> bids_;   // ascending
    std::map<Price, Quantity> asks_;   // ascending

    static double totalVolume(const std::map<Price, Quantity>& levels, int depth) {
        double vol = 0;
        int i = 0;
        for (const auto& [price, qty] : levels) {
            vol += qty;
            if (++i >= depth) break;
        }
        return vol;
    }
};

} // namespace cmf