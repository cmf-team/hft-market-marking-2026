// Тесты Avellaneda-Stoikov стратегий: microprice tilt и inventory skew.

#include "strategy/avellaneda_stoikov_strategy.hpp"
#include "test_helpers.hpp"

#include "catch2/catch_all.hpp"

using namespace hft_backtest;
using hft_backtest::test::make_snap;

TEST_CASE("AvellanedaStoikovMicroStrategy: microprice tilts toward thinner side",
          "[strategy]")
{
    AvellanedaStoikovConfig cfg;
    cfg.gamma = 0.1;
    cfg.k = 1.5;
    cfg.order_size = 1;
    cfg.tick_size_cents = 1;
    AvellanedaStoikovMicroStrategy mic(cfg);
    AvellanedaStoikovStrategy mid(cfg);

    // bid=1000, ask=1010, ask тонкий (1) -> microprice >> mid
    auto snap = make_snap(1'000'000, {{1000, 100}}, {{1010, 1}});
    (void)mid.on_market_data(snap, 1'000'000);
    (void)mic.on_market_data(snap, 1'000'000);

    REQUIRE(mid.last_reference() > 0);
    REQUIRE(mic.last_reference() > mid.last_reference());
}

TEST_CASE("AS-2008: positive inventory shifts reservation price down",
          "[strategy]")
{
    AvellanedaStoikovConfig cfg;
    cfg.gamma = 0.5;
    cfg.k = 1.5;
    cfg.order_size = 1;
    cfg.tick_size_cents = 1;
    cfg.sigma_window = 10;
    cfg.enable_inventory_skew = true;
    AvellanedaStoikovStrategy as(cfg);

    // Прокачиваем sigma серией снапшотов с РАЗНЫМ mid -- иначе sigma=0 и
    // стратегия не котирует.
    int wiggle[] = {0, 2, -1, 3, -2, 1, -3, 2};
    for (int i = 0; i < 8; ++i)
    {
        const Price mid_off = static_cast<Price>(1005 + wiggle[i]);
        auto s = make_snap(1'000'000ull + static_cast<uint64_t>(i) * 1000,
                           {{static_cast<Price>(mid_off - 5), 50}},
                           {{static_cast<Price>(mid_off + 5), 50}});
        (void)as.on_market_data(s, s.timestamp_us);
    }
    const double r0 = as.last_reservation();
    REQUIRE(r0 > 0);

    // "Получаем" длинную позицию через on_fill -- reservation должен поехать
    // вниз относительно reference price.
    FillReport f{1, Side::BUY, 1005, 10, 1'000'000};
    as.on_fill(f);
    auto s = make_snap(1'010'000, {{1000, 50}}, {{1010, 50}});
    (void)as.on_market_data(s, s.timestamp_us);
    REQUIRE(as.last_reservation() < as.last_reference());
}
