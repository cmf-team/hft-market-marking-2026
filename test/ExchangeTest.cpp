// tests for Exchange

#include "common/Exchange.hpp"
#include "common/Types.hpp"

#include "catch2/catch_all.hpp"

using namespace cmf;

namespace
{

LOBSnapshot makeBook(Price bid, Price ask, Quantity depth = 100.0)
{
    LOBSnapshot lob{};
    lob.timestamp = 1000;
    for (int i = 0; i < kLobDepth; ++i)
    {
        lob.asks[i].price = ask + i;
        lob.asks[i].amount = depth;
        lob.bids[i].price = bid - i;
        lob.bids[i].amount = depth;
    }
    return lob;
}

} // namespace

TEST_CASE("Exchange - market buy fills at best ask", "[Exchange]")
{
    Exchange ex;
    ex.onLobUpdate(makeBook(99.0, 101.0));
    ex.placeOrder(Side::Buy, OrderType::Market, 0.0, 1.0);

    auto fills = ex.pollFills();
    REQUIRE(fills.size() == 1);
    REQUIRE(fills[0].side == Side::Buy);
    REQUIRE(fills[0].price == Catch::Approx(101.0));
    REQUIRE(fills[0].quantity == Catch::Approx(1.0));
}

TEST_CASE("Exchange - sell-aggressor trade hits resting buy limit", "[Exchange]")
{
    Exchange ex;
    ex.onLobUpdate(makeBook(99.0, 101.0));

    // We rest a buy limit inside the spread.
    auto id = ex.placeOrder(Side::Buy, OrderType::Limit, 100.0, 1.0);
    REQUIRE(ex.pollFills().empty());
    REQUIRE(ex.activeOrderCount() == 1);

    // Aggressive sell at 99.5 — below our 100.0 — must hit us.
    ex.onTrade({1100, Side::Sell, 99.5, 1.0});
    auto fills = ex.pollFills();

    REQUIRE(fills.size() == 1);
    REQUIRE(fills[0].order_id == id);
    REQUIRE(fills[0].side == Side::Buy);
    REQUIRE(fills[0].price == Catch::Approx(100.0)); // fills at our limit, not trade.price
    REQUIRE(ex.activeOrderCount() == 0);
}

TEST_CASE("Exchange - buy-aggressor trade lifts resting sell limit", "[Exchange]")
{
    Exchange ex;
    ex.onLobUpdate(makeBook(99.0, 101.0));
    ex.placeOrder(Side::Sell, OrderType::Limit, 100.0, 1.0);

    ex.onTrade({1100, Side::Buy, 100.5, 1.0});
    auto fills = ex.pollFills();

    REQUIRE(fills.size() == 1);
    REQUIRE(fills[0].side == Side::Sell);
    REQUIRE(fills[0].price == Catch::Approx(100.0));
}

TEST_CASE("Exchange - cancelOrder removes resting order", "[Exchange]")
{
    Exchange ex;
    ex.onLobUpdate(makeBook(99.0, 101.0));
    auto id = ex.placeOrder(Side::Buy, OrderType::Limit, 100.0, 1.0);
    REQUIRE(ex.activeOrderCount() == 1);
    REQUIRE(ex.cancelOrder(id));
    REQUIRE(ex.activeOrderCount() == 0);

    ex.onTrade({1100, Side::Sell, 99.5, 1.0});
    REQUIRE(ex.pollFills().empty());
}

TEST_CASE("Exchange - partial fill across LOB depth for market order", "[Exchange]")
{
    LOBSnapshot lob{};
    lob.timestamp = 1000;
    lob.asks[0] = {101.0, 2.0};
    lob.asks[1] = {102.0, 3.0};
    for (int i = 2; i < kLobDepth; ++i)
        lob.asks[i] = {0.0, 0.0};
    for (int i = 0; i < kLobDepth; ++i)
        lob.bids[i] = {99.0 - i, 100.0};

    Exchange ex;
    ex.onLobUpdate(lob);

    ex.placeOrder(Side::Buy, OrderType::Market, 0.0, 4.0);
    auto fills = ex.pollFills();
    REQUIRE(fills.size() == 2);
    REQUIRE(fills[0].quantity == Catch::Approx(2.0));
    REQUIRE(fills[0].price == Catch::Approx(101.0));
    REQUIRE(fills[1].quantity == Catch::Approx(2.0));
    REQUIRE(fills[1].price == Catch::Approx(102.0));
}
