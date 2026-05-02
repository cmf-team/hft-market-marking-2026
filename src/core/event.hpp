#pragma once

#include "common/BasicTypes.hpp"

namespace cmf {

struct Trade {
    NanoTime ts{0};
    Side     side{Side::Buy};
    Price    price{0.0};
    Quantity amount{0.0};
};

}  // namespace cmf
