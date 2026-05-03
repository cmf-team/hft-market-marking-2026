#include "strategy/Microprice.hpp"

#include "catch2/catch_all.hpp"

using namespace cmf;

static L2Snapshot make_book(double bidPx, double bidQty, double askPx, double askQty)
{
    L2Snapshot s;
    s.bids.push_back({bidPx, bidQty});
    s.asks.push_back({askPx, askQty});
    return s;
}

TEST_CASE("mid - balanced book", "[Microprice]")
{
    auto s = make_book(100.0, 10.0, 102.0, 10.0);
    REQUIRE(mid(s) == Catch::Approx(101.0));
}

TEST_CASE("mid - empty book returns 0", "[Microprice]")
{
    L2Snapshot s;
    REQUIRE(mid(s) == 0.0);
}

TEST_CASE("microprice - balanced book equals mid", "[Microprice]")
{
    auto s = make_book(100.0, 5.0, 102.0, 5.0);
    REQUIRE(microprice(s) == Catch::Approx(mid(s)));
}

TEST_CASE("microprice - heavy bid side pushes toward ask", "[Microprice]")
{
    auto s = make_book(100.0, 9.0, 102.0, 1.0);
    const double mp = microprice(s);
    REQUIRE(mp > mid(s));
    REQUIRE(mp < 102.0);
}

TEST_CASE("microprice - heavy ask side pushes toward bid", "[Microprice]")
{
    auto s = make_book(100.0, 1.0, 102.0, 9.0);
    const double mp = microprice(s);
    REQUIRE(mp < mid(s));
    REQUIRE(mp > 100.0);
}

TEST_CASE("microprice - empty book returns 0", "[Microprice]")
{
    L2Snapshot s;
    REQUIRE(microprice(s) == 0.0);
}

TEST_CASE("microprice - zero total qty falls back to mid", "[Microprice]")
{
    auto s = make_book(100.0, 0.0, 102.0, 0.0);
    REQUIRE(microprice(s) == Catch::Approx(101.0));
}
