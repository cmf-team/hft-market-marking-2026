#pragma once
#include "BasicTypes.hpp"
#include <cmath>

namespace cmf {

struct StratParams {
    double gamma = 0.01;          // risk aversion
    double sigma = 0.02;          // volatility (std dev per second)
    double timeHorizonSec = 300;  // session end in seconds
    double spreadConst = 0.01;    // (optional) base spread
};

class AvellanedaStoikov {
public:
    AvellanedaStoikov(const StratParams& p = {}) : params_(p) {}

    struct Quote {
        Price bid = 0;   // <-- default initialisation
        Price ask = 0;   // <-- default initialisation
    };

    // Main quoting function
    Quote computeQuotes(Price referencePrice, Quantity inventory, double timeRemainingSec) {
        double riskTerm = inventory * params_.gamma * params_.sigma * params_.sigma * timeRemainingSec;
        Price reservation = referencePrice - riskTerm;

        // Optimal spread (simplified from the paper)
        double spread = params_.gamma * params_.sigma * params_.sigma * timeRemainingSec
                        + 2.0 / std::log(1.0 + params_.gamma / 0.005);

        Quote q;
        q.bid = reservation - spread / 2.0;
        q.ask = reservation + spread / 2.0;
        return q;
    }

    const StratParams& params() const { return params_; }

private:
    StratParams params_;
};

} // namespace cmf