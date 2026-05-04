#include <benchmark/benchmark.h>

#include "backtest/order_book/order_book.hpp"
#include "common/BasicTypes.hpp"

static constexpr int N = 64;
static constexpr int64_t BID_BASE = 10'000'000;
static constexpr int64_t ASK_BASE = 10'100'000;
static constexpr int64_t TICK = 10'000;

// Helper to create an order with sensible defaults
static Order make_order(uint64_t id, Side side, int64_t price,
                        int64_t qty = 100) noexcept
{
    Order o{};
    o.id = id;
    o.side = side;
    o.price = price;
    o.qty = qty;
    return o;
}

// Populate order book with N buy + N sell orders at distinct price levels
template <OrderBookLike OB>
static void populate(OB& ob, int n = N)
{
    for (int i = 0; i < n; ++i)
    {
        ob.submit(make_order(
            static_cast<uint64_t>(i),
            Side::Buy,
            BID_BASE - static_cast<int64_t>(i) * TICK));
        ob.submit(make_order(
            static_cast<uint64_t>(n + i),
            Side::Sell,
            ASK_BASE + static_cast<int64_t>(i) * TICK));
    }
}

// Benchmark: submit() throughput with N buy + N sell orders
template <OrderBookLike OrderBookT>
static void BM_Submit(benchmark::State& state)
{
    const int n = static_cast<int>(state.range(0));
    OrderBookT ob;

    for (auto _ : state)
    {
        for (int i = 0; i < n; ++i)
        {
            ob.submit(make_order(
                static_cast<uint64_t>(i),
                Side::Buy,
                BID_BASE - static_cast<int64_t>(i) * TICK));
            ob.submit(make_order(
                static_cast<uint64_t>(n + i),
                Side::Sell,
                ASK_BASE + static_cast<int64_t>(i) * TICK));
        }
        benchmark::DoNotOptimize(ob);

        state.PauseTiming();
        ob.drain(Side::Buy);
        ob.drain(Side::Sell);
        state.ResumeTiming();
    }

    state.SetItemsProcessed(state.iterations() * n * 2);
}

// Benchmark: match() on non-empty side (found path)
template <OrderBookLike OrderBookT>
static void BM_MatchFound(benchmark::State& state)
{
    OrderBookT ob;
    ob.submit(make_order(1, Side::Buy, BID_BASE));

    for (auto _ : state)
    {
        bool found = ob.match(Side::Buy, [](Order& o)
                              { benchmark::DoNotOptimize(o.remaining()); });
        benchmark::DoNotOptimize(found);
    }

    state.SetItemsProcessed(state.iterations());
}

// Benchmark: match() on empty side (fast-path false return)
template <OrderBookLike OrderBookT>
static void BM_MatchEmpty(benchmark::State& state)
{
    OrderBookT ob;
    // ob is default-constructed and never populated — both sides empty

    for (auto _ : state)
    {
        bool found = ob.match(Side::Buy, [](Order&) {});
        benchmark::DoNotOptimize(found);
    }

    state.SetItemsProcessed(state.iterations());
}

// Benchmark: empty() on non-empty side
template <OrderBookLike OrderBookT>
static void BM_EmptyNonEmpty(benchmark::State& state)
{
    OrderBookT ob;
    ob.submit(make_order(1, Side::Buy, BID_BASE));

    for (auto _ : state)
    {
        bool result = ob.empty(Side::Buy);
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}

// Benchmark: empty() on empty side
template <OrderBookLike OrderBookT>
static void BM_EmptyEmpty(benchmark::State& state)
{
    OrderBookT ob;

    for (auto _ : state)
    {
        bool result = ob.empty(Side::Buy);
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}

// Benchmark: drain() cost for N orders on each side
template <OrderBookLike OrderBookT>
static void BM_Drain(benchmark::State& state)
{
    const int n = static_cast<int>(state.range(0));

    for (auto _ : state)
    {
        OrderBookT ob;
        state.PauseTiming();
        populate(ob, n);
        state.ResumeTiming();

        // Timed: drain both sides
        ob.drain(Side::Buy);
        ob.drain(Side::Sell);
        benchmark::DoNotOptimize(ob);
    }

    state.SetItemsProcessed(state.iterations() * n * 2);
}

// Registration macros
#define REGISTER_OB(fn)                \
    BENCHMARK_TEMPLATE(fn, OrderBook)  \
        ->Unit(benchmark::kNanosecond) \
        ->Repetitions(3)               \
        ->DisplayAggregatesOnly()

#define REGISTER_OB_N(fn, n)           \
    BENCHMARK_TEMPLATE(fn, OrderBook)  \
        ->Arg(n)                       \
        ->Unit(benchmark::kNanosecond) \
        ->Repetitions(3)               \
        ->DisplayAggregatesOnly()

// Register all benchmarks
REGISTER_OB_N(BM_Submit, N);
REGISTER_OB(BM_MatchFound);
REGISTER_OB(BM_MatchEmpty);
REGISTER_OB(BM_EmptyNonEmpty);
REGISTER_OB(BM_EmptyEmpty);
REGISTER_OB_N(BM_Drain, N);
