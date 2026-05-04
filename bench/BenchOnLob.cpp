#include <benchmark/benchmark.h>

#include "backtest/order_book/order_book.hpp"
#include "backtest/strategies/factory.hpp"
#include "common/BasicTypes.hpp"

// ---------------------------------------------------------------------------
// Synthetic LOB snapshot — realistic mid-market with 25 levels
// ---------------------------------------------------------------------------
static LobSnapshot make_lob_snapshot() noexcept
{
    LobSnapshot lob{};
    lob.timestamp = 1'700'000'000'000'000ULL; // realistic microsecond timestamp

    for (int32_t i = 0; i < LOB_DEPTH; ++i)
    {
        // Asks: best ask at 11.0000, widen by 0.001 per level
        lob.asks[i] = {
            11'000'000 + i * 10'000,
            1'000LL + static_cast<int64_t>(i) * 100};

        // Bids: best bid at 10.9900, widen by 0.001 per level
        lob.bids[i] = {
            10'990'000 - i * 10'000,
            1'000LL + static_cast<int64_t>(i) * 100};
    }

    return lob;
}

// ---------------------------------------------------------------------------
// PassiveStrategy on_lob benchmark
// Seed pnl.position so the unconditional-drain branch (position == 0) is
// skipped after the first fill-free iteration; subsequent iterations hit the
// stale-price-check + conditional-resubmit hot path.
// ---------------------------------------------------------------------------
template <typename OrderBookT>
static void BM_OnLob_Passive(benchmark::State& state)
{
    const LobSnapshot lob = make_lob_snapshot();
    PassiveStrategy strategy(1'000);
    OrderBookT ob;
    PnlState pnl{};
    pnl.position = 500; // non-zero: bypass unconditional drain each iteration

    for (auto _ : state)
    {
        strategy.template on_lob<OrderBookT>(lob, ob, pnl);
        benchmark::DoNotOptimize(ob); // ob.submit() is the primary side effect
        benchmark::DoNotOptimize(pnl);
    }

    state.SetItemsProcessed(state.iterations());
}

// ---------------------------------------------------------------------------
// TrendFollowingStrategy on_lob benchmark
// A prototype strategy is warmed up once outside the timed loop.  Each
// iteration copies the warm prototype so every call sees identical EMA state,
// making latency measurements comparable across repetitions.
// ---------------------------------------------------------------------------
template <typename OrderBookT>
static void BM_OnLob_TrendEma(benchmark::State& state)
{
    const LobSnapshot lob = make_lob_snapshot();

    // Build and warm up the prototype (2500 default warmup ticks + 100 extra)
    TrendFollowingStrategy warmed(1'000);
    {
        OrderBookT dummy_ob;
        PnlState dummy_pnl{};
        for (int i = 0; i < 2600; ++i)
        {
            warmed.template on_lob<OrderBookT>(lob, dummy_ob, dummy_pnl);
        }
    }

    // ob and pnl persist across iterations.  With a fixed LOB snapshot, both
    // EMAs converge to the same mid-price and never cross — the timed loop
    // always exercises the no-signal early-return path.
    OrderBookT ob;
    PnlState pnl{};
    TrendFollowingStrategy strategy = warmed; // warm initial state

    for (auto _ : state)
    {
        strategy.template on_lob<OrderBookT>(lob, ob, pnl);
        // DoNotOptimize strategy: fast_ema_/slow_ema_ are the only outputs of
        // the no-crossover path.  Without this the compiler can eliminate the
        // call body as a dead store since ob and pnl are not written.
        benchmark::DoNotOptimize(strategy);
    }

    state.SetItemsProcessed(state.iterations());
}

// ---------------------------------------------------------------------------
// Registration macros
// ---------------------------------------------------------------------------
#define REGISTER_LOB_BENCHMARK(ob_name, ob_type, fn_suffix, st_label) \
    BENCHMARK_TEMPLATE(BM_OnLob_##fn_suffix, ob_type)                 \
        ->Unit(benchmark::kNanosecond)                                \
        ->Name("BM_OnLob<" #ob_name "," #st_label ">")                \
        ->Repetitions(3)                                              \
        ->DisplayAggregatesOnly()

REGISTER_LOB_BENCHMARK(OrderBook, OrderBook, Passive, passive);
REGISTER_LOB_BENCHMARK(OrderBook, OrderBook, TrendEma, trend_ema);

// benchmark::benchmark_main (linked via CMake) provides main() automatically.
