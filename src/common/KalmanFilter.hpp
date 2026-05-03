#pragma once
#include "BasicTypes.hpp"

namespace cmf {

class KalmanFilter {
public:
    KalmanFilter(double processNoise = 1e-7, double measurementNoise = 1e-5,
                 double initialState = 0, double initialError = 1.0)
        : state_(initialState), errorCov_(initialError),
          Q_(processNoise), R_(measurementNoise) {}

    void update(Price measurement) {
        // Prediction
        errorCov_ += Q_;
        // Update
        double K = errorCov_ / (errorCov_ + R_);
        state_ += K * (measurement - state_);
        errorCov_ *= (1 - K);
    }

    Price predict() const { return state_; }

private:
    double state_;
    double errorCov_;
    double Q_, R_;
};

} // namespace cmf