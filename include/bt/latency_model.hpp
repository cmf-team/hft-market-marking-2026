#pragma once

#include "bt/types.hpp"

namespace bt {

// Pluggable latency model. Three independent delays in microseconds:
//   - submit: strategy → matcher (post a new order)
//   - cancel: strategy → matcher (cancel a resting order)
//   - fill  : matcher → strategy (notify of fill or rejection)
//
// Each delay is asked at the moment the event enters the latency layer
// (i.e. with `now` set to the wall-clock at enqueue time). Future jittered
// or distribution-based models can use `now` as a seed; fixed models ignore it.
struct ILatencyModel {
    virtual ~ILatencyModel() = default;

    [[nodiscard]] virtual Timestamp submit_delay(Timestamp now) const noexcept = 0;
    [[nodiscard]] virtual Timestamp cancel_delay(Timestamp now) const noexcept = 0;
    [[nodiscard]] virtual Timestamp fill_delay(Timestamp now)   const noexcept = 0;
};

// Constant delays for every event of a given type. Stateless and trivially
// shareable.
class FixedLatencyModel final : public ILatencyModel {
public:
    constexpr FixedLatencyModel(Timestamp submit_us, Timestamp cancel_us,
                                Timestamp fill_us) noexcept
        : submit_us_(submit_us), cancel_us_(cancel_us), fill_us_(fill_us) {}

    [[nodiscard]] Timestamp submit_delay(Timestamp /*now*/) const noexcept override { return submit_us_; }
    [[nodiscard]] Timestamp cancel_delay(Timestamp /*now*/) const noexcept override { return cancel_us_; }
    [[nodiscard]] Timestamp fill_delay(Timestamp /*now*/)   const noexcept override { return fill_us_; }

private:
    Timestamp submit_us_;
    Timestamp cancel_us_;
    Timestamp fill_us_;
};

}  // namespace bt
