#include "strategy/Volatility.hpp"

#include "catch2/catch_all.hpp"

#include <cmath>

using namespace cmf;

TEST_CASE("EwmaVariance - not initialized after 1 price", "[Volatility]")
{
    EwmaVariance ev(100.0);
    ev.update(1.0);
    REQUIRE_FALSE(ev.initialized());
}

TEST_CASE("EwmaVariance - initialized after 3 prices", "[Volatility]")
{
    // First call sets prevLogPx, second produces 1 return (n=1), third produces n=2 -> initialized
    EwmaVariance ev(100.0);
    ev.update(1.0);
    REQUIRE_FALSE(ev.initialized());
    ev.update(1.01);
    REQUIRE_FALSE(ev.initialized()); // n=1, needs n>=2
    ev.update(1.02);
    REQUIRE(ev.initialized()); // n=2
}

TEST_CASE("EwmaVariance - zero variance for flat prices", "[Volatility]")
{
    EwmaVariance ev(100.0);
    for (int i = 0; i < 200; ++i)
        ev.update(100.0);
    REQUIRE(ev.value() == Catch::Approx(0.0).margin(1e-12));
}

TEST_CASE("EwmaVariance - converges to known variance", "[Volatility]")
{
    // Feed alternating returns of +r, -r. True per-step variance = r^2.
    const double r = 0.001;
    const double halfLife = 50.0;
    EwmaVariance ev(halfLife);
    double px = 1.0;
    for (int i = 0; i < 2000; ++i)
    {
        px *= (i % 2 == 0) ? (1.0 + r) : (1.0 / (1.0 + r));
        ev.update(px);
    }
    const double logR = std::log(1.0 + r);
    REQUIRE(ev.value() == Catch::Approx(logR * logR).epsilon(0.15));
}

TEST_CASE("EwmaVariance - respects cap", "[Volatility]")
{
    const double cap = 1e-4;
    EwmaVariance ev(100.0, cap);
    ev.update(1.0);
    ev.update(1000.0); // huge jump
    REQUIRE(ev.value() <= cap + 1e-15);
}

TEST_CASE("EwmaVariance - ignores non-positive prices", "[Volatility]")
{
    EwmaVariance ev(100.0);
    ev.update(1.0);      // sets prevLogPx, n=0
    ev.update(0.0);      // skipped
    ev.update(-1.0);     // skipped
    REQUIRE_FALSE(ev.initialized()); // still n=0
    ev.update(1.01);     // n=1
    REQUIRE_FALSE(ev.initialized()); // n=1 < 2
    ev.update(1.02);     // n=2
    REQUIRE(ev.initialized());
}
