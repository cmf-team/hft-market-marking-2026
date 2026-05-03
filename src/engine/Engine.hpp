#pragma once

#include "engine/LatencyModel.hpp"
#include "engine/Logger.hpp"
#include "engine/Metrics.hpp"
#include "engine/OrderBook.hpp"
#include "engine/Types.hpp"
#include <functional>
#include <memory>
#include <queue>
#include <unordered_map>
#include <vector>

namespace cmf
{

// Minimal reader interfaces so engine has no dependency on io module.
struct IL2Reader
{
    virtual ~IL2Reader() = default;
    virtual bool next(L2Snapshot& out) = 0;
};

struct ITradesReader
{
    virtual ~ITradesReader() = default;
    virtual bool next(TradeEvent& out) = 0;
};

struct StrategyContext;

struct Strategy
{
    virtual ~Strategy() = default;
    virtual void on_l2(const L2Snapshot& l2, StrategyContext& ctx) = 0;
    virtual void on_trade(const TradeEvent& t, StrategyContext& ctx) = 0;
    virtual void on_fill(const ExecReport&, Side, bool /*isMaker*/, StrategyContext&) {}
};

struct StrategyContext
{
    std::function<ClOrdId(const Order&)> send_order;
    std::function<void(ClOrdId)> cancel_order;
};

struct Event
{
    NanoTime ts{0};
    std::uint64_t seq{0};
    std::function<void()> fn;

    bool operator>(const Event& o) const
    {
        return ts > o.ts || (ts == o.ts && seq > o.seq);
    }
};

class BacktestEngine
{
public:
    BacktestEngine(std::unique_ptr<LatencyModel> latency,
                   std::unique_ptr<Strategy> strategy,
                   Fees fees = {});

    void set_l2_reader(std::unique_ptr<IL2Reader> r) { l2_ = std::move(r); }
    void set_trades_reader(std::unique_ptr<ITradesReader> r) { trades_ = std::move(r); }

    void run();

    const std::vector<TradeRecord>& executions() const { return execs_; }
    const Portfolio& portfolio() const { return pf_; }
    const std::vector<double>& equity_series() const { return equity_; }
    const std::vector<double>& unrealized_returns_series() const { return unrealRets_; }
    const std::vector<NanoTime>& equity_ts_series() const { return equityTs_; }
    const std::vector<double>& inventory_series() const { return inventory_; }
    std::uint64_t sent_count() const { return totalSent_; }
    std::uint64_t filled_count() const { return totalFilled_; }
    double estimated_steps_per_year() const;

private:
    void schedule(NanoTime when, std::function<void()> fn);
    ClOrdId place_order(Order o);
    void cancel(ClOrdId id);
    void apply_fill(const Order& o, const ExecReport& rep, bool isMaker);
    void recompute_equity(NanoTime ts, double midPx);
    void process_live_orders_on_l2();
    void process_live_orders_on_trade(const TradeEvent& t);

    std::unique_ptr<LatencyModel> latency_;
    std::unique_ptr<Strategy> strat_;
    std::unique_ptr<IL2Reader> l2_;
    std::unique_ptr<ITradesReader> trades_;
    Fees fees_;

    L2Snapshot lastBook_{};
    std::priority_queue<Event, std::vector<Event>, std::greater<Event>> q_;
    StrategyContext ctx_;

    std::vector<TradeRecord> execs_;
    ClOrdId nextOrderId_{1};
    Portfolio pf_{};
    std::unordered_map<ClOrdId, Order> liveOrders_;

    std::vector<double> equity_;
    std::vector<double> unrealRets_;
    std::vector<NanoTime> equityTs_;
    std::vector<double> inventory_;

    double lastEquity_{0.0};
    double lastMid_{0.0};
    NanoTime lastTs_{-1};
    long double sumDtNs_{0.0L};
    std::size_t dtSamples_{0};
    std::uint64_t seqCounter_{0};
    std::uint64_t totalSent_{0};
    std::uint64_t totalFilled_{0};
};

} // namespace cmf
