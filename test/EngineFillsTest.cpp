#include "engine/Engine.hpp"
#include "engine/LatencyModel.hpp"

#include "catch2/catch_all.hpp"

using namespace cmf;

namespace
{

struct RecordingStrategy : Strategy
{
    std::vector<ExecReport> fills;
    std::vector<bool> makerFlags;

    void on_l2(const L2Snapshot&, StrategyContext&) override {}
    void on_trade(const TradeEvent&, StrategyContext&) override {}
    void on_fill(const ExecReport& rep, Side, bool isMaker, StrategyContext&) override
    {
        fills.push_back(rep);
        makerFlags.push_back(isMaker);
    }
};

struct FeedL2 : IL2Reader
{
    std::vector<L2Snapshot> data;
    std::size_t pos{0};
    bool next(L2Snapshot& out) override
    {
        if (pos >= data.size())
            return false;
        out = data[pos++];
        return true;
    }
};

struct FeedTrades : ITradesReader
{
    std::vector<TradeEvent> data;
    std::size_t pos{0};
    bool next(TradeEvent& out) override
    {
        if (pos >= data.size())
            return false;
        out = data[pos++];
        return true;
    }
};

L2Snapshot make_l2(NanoTime ts, double bid, double bidQty, double ask, double askQty)
{
    L2Snapshot s;
    s.ts = ts;
    s.bids.push_back({bid, bidQty});
    s.asks.push_back({ask, askQty});
    return s;
}

} // namespace

TEST_CASE("Limit order filled passively on trade - trade qty cap", "[Engine]")
{
    // Resting buy limit at 100 for qty=10. A sell trade hits at price 100, size 3.
    // Fill should be capped at trade qty = 3, not order qty = 10.
    struct LimitBuyStrategy : Strategy
    {
        ExecReport fill{};
        bool filled{false};
        bool placed{false};

        void on_l2(const L2Snapshot& l2, StrategyContext& ctx) override
        {
            if (!placed)
            {
                Order o{0, OrderType::Limit, Side::Buy, 100.0, 10.0, l2.ts};
                ctx.send_order(o);
                placed = true;
            }
        }
        void on_trade(const TradeEvent&, StrategyContext&) override {}
        void on_fill(const ExecReport& rep, Side, bool, StrategyContext&) override
        {
            fill = rep;
            filled = true;
        }
        ~LimitBuyStrategy() override = default;
    };

    auto strat = std::make_unique<LimitBuyStrategy>();
    LimitBuyStrategy* stratPtr = strat.get();

    BacktestEngine eng(std::make_unique<ConstLatency>(0), std::move(strat), Fees{0.0, 0.0});

    auto l2Feed = std::make_unique<FeedL2>();
    l2Feed->data.push_back(make_l2(1000, 99.0, 5.0, 101.0, 5.0));
    l2Feed->data.push_back(make_l2(3000, 99.0, 5.0, 101.0, 5.0));

    auto tradeFeed = std::make_unique<FeedTrades>();
    tradeFeed->data.push_back({2000, Side::Sell, 100.0, 3.0});

    eng.set_l2_reader(std::move(l2Feed));
    eng.set_trades_reader(std::move(tradeFeed));
    eng.run();

    REQUIRE(stratPtr->filled);
    REQUIRE(stratPtr->fill.filled == Catch::Approx(3.0));
}

TEST_CASE("Market order executes against best ask", "[Engine]")
{
    struct MarketBuyStrategy : Strategy
    {
        ExecReport fill{};
        bool filled{false};
        bool firstTick{true};

        void on_l2(const L2Snapshot& l2, StrategyContext& ctx) override
        {
            if (firstTick)
            {
                Order o{0, OrderType::Market, Side::Buy, 0.0, 5.0, l2.ts};
                ctx.send_order(o);
                firstTick = false;
            }
        }
        void on_trade(const TradeEvent&, StrategyContext&) override {}
        void on_fill(const ExecReport& rep, Side, bool, StrategyContext&) override
        {
            fill = rep;
            filled = true;
        }
        ~MarketBuyStrategy() override = default;
    };

    auto strat = std::make_unique<MarketBuyStrategy>();
    MarketBuyStrategy* stratPtr = strat.get();

    BacktestEngine eng(std::make_unique<ConstLatency>(0), std::move(strat), Fees{0.0, 0.0});

    auto l2Feed = std::make_unique<FeedL2>();
    l2Feed->data.push_back(make_l2(1000, 99.0, 10.0, 101.0, 10.0));
    l2Feed->data.push_back(make_l2(2000, 99.0, 10.0, 101.0, 10.0));
    eng.set_l2_reader(std::move(l2Feed));

    eng.run();

    REQUIRE(stratPtr->filled);
    REQUIRE(stratPtr->fill.filled == Catch::Approx(5.0));
    REQUIRE(stratPtr->fill.avgPrice == Catch::Approx(101.0));
}

TEST_CASE("Cancel prevents fill", "[Engine]")
{
    struct CancelStrategy : Strategy
    {
        bool cancelled{false};
        bool filled{false};
        ClOrdId orderId{0};

        void on_l2(const L2Snapshot& l2, StrategyContext& ctx) override
        {
            if (orderId == 0)
            {
                Order o{0, OrderType::Limit, Side::Buy, 101.0, 5.0, l2.ts};
                orderId = ctx.send_order(o);
            }
            else if (!cancelled)
            {
                ctx.cancel_order(orderId);
                cancelled = true;
            }
        }
        void on_trade(const TradeEvent&, StrategyContext&) override {}
        void on_fill(const ExecReport&, Side, bool, StrategyContext&) override { filled = true; }
        ~CancelStrategy() override = default;
    };

    auto strat = std::make_unique<CancelStrategy>();
    CancelStrategy* stratPtr = strat.get();

    // Zero latency: cancel at tick 2 should prevent fill at tick 3
    BacktestEngine eng(std::make_unique<ConstLatency>(0), std::move(strat), Fees{0.0, 0.0});

    auto l2Feed = std::make_unique<FeedL2>();
    l2Feed->data.push_back(make_l2(1000, 99.0, 5.0, 102.0, 5.0));
    l2Feed->data.push_back(make_l2(2000, 99.0, 5.0, 102.0, 5.0));
    l2Feed->data.push_back(make_l2(3000, 100.5, 5.0, 101.0, 5.0));
    eng.set_l2_reader(std::move(l2Feed));

    eng.run();

    REQUIRE(stratPtr->cancelled);
    REQUIRE_FALSE(stratPtr->filled);
}
