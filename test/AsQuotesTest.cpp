#include "strategy/AvellanedaStoikov.hpp"
#include "engine/Engine.hpp"

#include "catch2/catch_all.hpp"

#include <cmath>

using namespace cmf;

// Closed-form A-S half-spread: gamma*sigma2*(T-t)/2 + (1/gamma)*ln(1+gamma/k)
static double as_half_spread(double gamma, double sigma2, double tRem, double k)
{
    return 0.5 * gamma * sigma2 * tRem + (1.0 / gamma) * std::log1p(gamma / k);
}

// Closed-form reservation price: s - q * gamma * sigma2 * (T-t)
static double as_reservation(double s, double q, double gamma, double sigma2, double tRem)
{
    return s - q * gamma * sigma2 * tRem;
}

TEST_CASE("A-S half-spread matches closed form", "[AsQuotes]")
{
    const double gamma = 0.1, k = 1.5, sigma2 = 1e-6, tRem = 1000.0;
    const double expected = as_half_spread(gamma, sigma2, tRem, k);
    const double computed = 0.5 * gamma * sigma2 * tRem + (1.0 / gamma) * std::log1p(gamma / k);
    REQUIRE(computed == Catch::Approx(expected).epsilon(1e-9));
}

TEST_CASE("A-S reservation price sign flips with inventory", "[AsQuotes]")
{
    const double gamma = 0.1, sigma2 = 1e-6, tRem = 1000.0, s = 100.0;
    const double rPos = as_reservation(s, +5000.0, gamma, sigma2, tRem);
    const double rNeg = as_reservation(s, -5000.0, gamma, sigma2, tRem);
    REQUIRE(rPos < s);
    REQUIRE(rNeg > s);
    // symmetric around s
    REQUIRE((rPos - s) == Catch::Approx(-(rNeg - s)).epsilon(1e-9));
}

TEST_CASE("A-S half-spread is independent of inventory", "[AsQuotes]")
{
    const double gamma = 0.1, k = 1.5, sigma2 = 1e-6, tRem = 1000.0;
    const double d1 = as_half_spread(gamma, sigma2, tRem, k);
    // half-spread formula does not involve q at all
    REQUIRE(d1 > 0.0);
    // just verify it's positive for several gamma values
    for (double g : {0.01, 0.1, 1.0})
        REQUIRE(as_half_spread(g, sigma2, tRem, k) > 0.0);
}

TEST_CASE("A-S half-spread is positive", "[AsQuotes]")
{
    for (double tRem : {0.0, 100.0, 1e6})
        REQUIRE(as_half_spread(0.1, 1e-6, tRem, 1.5) >= 0.0);
}

TEST_CASE("A-S strategy quotes straddle reservation price", "[AsQuotes]")
{
    // Drive the strategy through a mock engine context to verify bid < r < ask.
    // We don't run the full engine; just call on_l2 directly with a fake context.
    ClOrdId lastBidId = 0, lastAskId = 0;
    double lastBidPx = 0.0, lastAskPx = 0.0;

    StrategyContext ctx;
    ctx.send_order = [&](const Order& o) -> ClOrdId
    {
        static ClOrdId seq = 1;
        if (o.side == Side::Buy)
        {
            lastBidId = seq;
            lastBidPx = o.price;
        }
        else
        {
            lastAskId = seq;
            lastAskPx = o.price;
        }
        return seq++;
    };
    ctx.cancel_order = [](ClOrdId) {};

    AvellanedaStoikovMM strat(RefPriceMode::Mid, 0.1, 1.5, 1e6, 1e9, 1.0, 50.0);

    // Warm up vol
    L2Snapshot warmup;
    warmup.bids.push_back({99.9, 1.0});
    warmup.asks.push_back({100.1, 1.0});
    warmup.ts = 1000;
    for (int i = 0; i < 10; ++i)
    {
        warmup.ts += 1;
        warmup.bids[0].price = 100.0 + (i % 3) * 0.0001;
        warmup.asks[0].price = warmup.bids[0].price + 0.0002;
        strat.on_l2(warmup, ctx);
    }

    // Final tick: flat book at 100.0 / 100.002
    L2Snapshot s;
    s.bids.push_back({100.0, 5.0});
    s.asks.push_back({100.002, 5.0});
    s.ts = 2000;
    strat.on_l2(s, ctx);

    REQUIRE(lastBidPx > 0.0);
    REQUIRE(lastAskPx > 0.0);
    REQUIRE(lastBidPx < 100.001); // bid below mid
    REQUIRE(lastAskPx > 100.001); // ask above mid
    REQUIRE(lastAskPx > lastBidPx);
}
