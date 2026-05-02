#pragma once

#include "common/BasicTypes.hpp"

namespace cmf {

// A fill of one of our own orders against the simulated venue.
struct Fill {
    NanoTime ts{0};
    Side     side{Side::Buy};
    Price    price{0.0};
    Quantity amount{0.0};
};

}  // namespace cmf
