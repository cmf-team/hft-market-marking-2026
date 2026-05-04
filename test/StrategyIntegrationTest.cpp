#include "catch2/catch_all.hpp"
#include "backtest/engine/engine.hpp"
#include "backtest/strategies/factory.hpp"
#include <cmath>
#include <filesystem>
#include <set>
#include <variant>

// Paths resolved relative to this source file at compile time.
static const std::string LOB_PATH =
    (std::filesystem::path(__FILE__).parent_path().parent_path() / "data/sample_lob.csv").string();
static const std::string TRADES_PATH =
    (std::filesystem::path(__FILE__).parent_path().parent_path() / "data/sample_trades.csv").string();

TEST_CASE("StrategyIntegrationTest - FullBacktest", "[StrategyIntegrationTest]")
{
    for (const auto st : ALL_STRATEGIES)
    {
        SECTION(std::string(strategy_name(st)))
        {
            MmapFile lob_file(LOB_PATH);
            MmapFile trades_file(TRADES_PATH);
            Config cfg{};
            constexpr int64_t qty = 1000;

            LobReader lob_rdr(lob_file);
            TradeReader trade_rdr(trades_file);
            StrategyVariant strategy = make_strategy(st, trade_rdr, qty);

            BacktestResult result;
            std::visit([&](auto& s)
                       { result = run_backtest(lob_rdr, s, cfg); }, strategy);

            // Verify backtest ran and produced valid data
            CHECK(result.lob_rows > 0);
            CHECK(result.fills.size() == result.pnl.total_fills);

            CHECK(result.pnl_series.size() == result.pnl_timestamps.size());
            CHECK(result.pnl_series.size() == result.lob_rows / cfg.pnl_sample_interval);

            CHECK(std::isfinite(result.pnl.realized_pnl));
            CHECK(std::isfinite(result.pnl.unrealized_pnl));

            // All fills must have valid qty and ordered timestamps
            for (size_t i = 0; i < result.fills.size(); ++i)
            {
                CHECK(result.fills[i].qty > 0);
                if (i > 0)
                {
                    CHECK(result.fills[i - 1].ts <= result.fills[i].ts);
                }
            }
        }
    }
}

TEST_CASE("StrategyNamesTest - AllEntriesHaveNames", "[StrategyNamesTest]")
{
    for (const auto st : ALL_STRATEGIES)
    {
        std::string_view name = strategy_name(st);
        CHECK(!name.empty());
    }
}

TEST_CASE("StrategyNamesTest - AllNamesAreUnique", "[StrategyNamesTest]")
{
    std::set<std::string_view> names;
    for (const auto st : ALL_STRATEGIES)
    {
        std::string_view name = strategy_name(st);
        CHECK(names.count(name) == 0);
        names.insert(name);
    }
    CHECK(names.size() == ALL_STRATEGIES.size());
}
