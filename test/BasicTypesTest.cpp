// tests for BasicTypes

#include "common/BasicTypes.hpp"

#include "catch2/catch_all.hpp"

TEST_CASE("BasicTypes - Side", "[BasicTypes]")
{
    REQUIRE(int(cmf::Side::Buy) == 1);
    REQUIRE(int(cmf::Side::Sell) == -1);
    REQUIRE(int(cmf::Side::None) == 0);
}
