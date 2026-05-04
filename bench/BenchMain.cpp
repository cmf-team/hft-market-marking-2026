#include <benchmark/benchmark.h>
#include <filesystem>

#include "backtest/engine/engine.hpp"
#include "backtest/strategies/factory.hpp"

// Paths resolved relative to this source file at compile time.
static const std::string LOB_PATH =
    (std::filesystem::path(__FILE__).parent_path().parent_path() / "data/lob.csv").string();
static const std::string TRADES_PATH =
    (std::filesystem::path(__FILE__).parent_path().parent_path() / "data/trades.csv").string();

// ---------------------------------------------------------------------------
// Template benchmark: varies both OrderBook implementation and StrategyType
// ---------------------------------------------------------------------------
template <typename OrderBookT, StrategyType ST>
static void BM_RunBacktest(benchmark::State& state)
{
    MmapFile lob_file(LOB_PATH);
    MmapFile trades_file(TRADES_PATH);
    Config cfg{};

    int64_t total_events = 0;
    for (auto _ : state)
    {
        LobReader lob_rdr(lob_file);
        TradeReader trade_rdr(trades_file);
        StrategyVariant strategy = make_strategy(ST, trade_rdr, cfg.target_qty);

        auto result = std::visit(
            [&](auto& s)
            { return run_backtest<OrderBookT>(lob_rdr, s, cfg); },
            strategy);
        benchmark::DoNotOptimize(result);

        const uint64_t trade_rows = std::visit([](const auto& s)
                                               { return s.trade_rows(); }, strategy);
        total_events = static_cast<int64_t>(result.lob_rows + trade_rows);
    }
    state.SetItemsProcessed(state.iterations() * total_events);
}

// ---------------------------------------------------------------------------
// Registration macros
// ---------------------------------------------------------------------------
// Register one (OrderBook, Strategy) combination
#define REGISTER_BENCHMARK(ob_name, ob_type, st_enum, st_label)        \
    BENCHMARK_TEMPLATE(BM_RunBacktest, ob_type, StrategyType::st_enum) \
        ->Unit(benchmark::kMillisecond)                                \
        ->Name("BM_RunBacktest<" #ob_name "," #st_label ">")           \
        ->Iterations(5)                                                \
        ->Repetitions(3)                                               \
        ->DisplayAggregatesOnly()

// Register BOTH strategies for a given OrderBook implementation
#define REGISTER_ORDERBOOK_BENCHMARK(impl_name, impl_type)                                    \
    REGISTER_BENCHMARK(impl_name, impl_type, Passive, passive);                               \
    REGISTER_BENCHMARK(impl_name, impl_type, TrendFollowing, trend_ema);                      \
    REGISTER_BENCHMARK(impl_name, impl_type, AvellanedaStoikovStrategy, trend_ema);           \
    REGISTER_BENCHMARK(impl_name, impl_type, MicropriceAvellanedaStoikovStrategy, trend_ema); \
    REGISTER_BENCHMARK(impl_name, impl_type, Replay, replay)

REGISTER_ORDERBOOK_BENCHMARK(OrderBook, OrderBook);
REGISTER_ORDERBOOK_BENCHMARK(OrderBookV2, OrderBookV2);

// benchmark::benchmark_main (linked via CMake) provides main() automatically.
