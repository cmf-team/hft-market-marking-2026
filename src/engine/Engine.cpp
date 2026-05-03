#include "engine/Engine.hpp"
#include <limits>

namespace cmf
{

BacktestEngine::BacktestEngine(std::unique_ptr<LatencyModel> latency,
                               std::unique_ptr<Strategy> strategy,
                               Fees fees)
    : latency_(std::move(latency)), strat_(std::move(strategy)), fees_(fees)
{
    ctx_.send_order = [this](const Order& o) { return place_order(o); };
    ctx_.cancel_order = [this](ClOrdId id) { cancel(id); };
}

void BacktestEngine::schedule(NanoTime when, std::function<void()> fn)
{
    q_.push(Event{when, seqCounter_++, std::move(fn)});
}

ClOrdId BacktestEngine::place_order(Order o)
{
    if (o.id == 0)
        o.id = nextOrderId_++;
    const NanoTime d = latency_ ? latency_->sample_delay(o.ts) : 0;
    const NanoTime execTs = o.ts + d;
    liveOrders_[o.id] = o;
    ++totalSent_;
    Logger::log("ORDER_NEW id=" + std::to_string(o.id) +
                " ts=" + std::to_string(o.ts) +
                " side=" + (o.side == Side::Buy ? "buy" : "sell") +
                " px=" + std::to_string(o.price) +
                " qty=" + std::to_string(o.amount));
    schedule(execTs, [this, o]() {
        if (!liveOrders_.count(o.id))
            return;
        ExecReport rep{};
        bool isMaker = false;
        if (o.type == OrderType::Market)
        {
            rep = OrderBook::execute_market(o, lastBook_);
            isMaker = false;
        }
        else if (OrderBook::limit_crossed(o, lastBook_))
        {
            rep = OrderBook::aggressor_fill(o, lastBook_);
            isMaker = false;
        }
        else
        {
            return; // resting
        }
        if (rep.filled > 0.0)
        {
            apply_fill(o, rep, isMaker);
            liveOrders_.erase(o.id);
        }
    });
    return o.id;
}

void BacktestEngine::run()
{
    L2Snapshot s{};
    TradeEvent t{};
    bool hasL2 = l2_ && l2_->next(s);
    bool hasT = trades_ && trades_->next(t);

    while (hasL2 || hasT || !q_.empty())
    {
        NanoTime nextDataTs = std::numeric_limits<NanoTime>::max();
        enum WhichNext
        {
            NONE,
            L2,
            TRADE
        } which = NONE;
        if (hasL2 && s.ts < nextDataTs)
        {
            nextDataTs = s.ts;
            which = L2;
        }
        if (hasT && t.ts < nextDataTs)
        {
            nextDataTs = t.ts;
            which = TRADE;
        }
        if (!q_.empty() && q_.top().ts <= nextDataTs)
        {
            auto ev = q_.top();
            q_.pop();
            ev.fn();
            continue;
        }
        if (which == L2)
        {
            lastBook_ = s;
            if (!s.asks.empty() && !s.bids.empty())
                lastMid_ = 0.5 * (s.asks.front().price + s.bids.front().price);
            process_live_orders_on_l2();
            strat_->on_l2(s, ctx_);
            if (lastMid_ != 0.0)
                recompute_equity(s.ts, lastMid_);
            hasL2 = l2_->next(s);
        }
        else if (which == TRADE)
        {
            process_live_orders_on_trade(t);
            strat_->on_trade(t, ctx_);
            hasT = trades_->next(t);
        }
        else
        {
            if (!q_.empty())
            {
                auto ev = q_.top();
                q_.pop();
                ev.fn();
            }
            else
            {
                break;
            }
        }
    }
}

void BacktestEngine::cancel(ClOrdId id)
{
    if (liveOrders_.erase(id))
        Logger::log("ORDER_CANCEL id=" + std::to_string(id));
}

void BacktestEngine::apply_fill(const Order& o, const ExecReport& rep, bool isMaker)
{
    const double notional = rep.avgPrice * rep.filled;
    const double feeRate = isMaker ? fees_.maker : fees_.taker;
    const double fee = notional * feeRate;

    const double eqBefore = (lastMid_ != 0.0) ? pf_.cash + pf_.position * lastMid_ : 0.0;

    execs_.push_back(TradeRecord{rep.ts, rep.filled, rep.avgPrice, o.side, fee});
    ++totalFilled_;

    const double signedQty = (o.side == Side::Buy ? 1.0 : -1.0) * rep.filled;
    pf_.cash -= signedQty * rep.avgPrice;
    pf_.cash -= fee;
    pf_.position += signedQty;

    if (lastMid_ != 0.0)
    {
        const double eq = pf_.cash + pf_.position * lastMid_;
        inventory_.push_back(pf_.position);
        equity_.push_back(eq);
        equityTs_.push_back(rep.ts);
        const double ret = (lastEquity_ != 0.0) ? (eq - lastEquity_) / std::abs(lastEquity_) : 0.0;
        unrealRets_.push_back(ret);
        lastEquity_ = eq;
        (void)eqBefore;
    }

    Logger::log("ORDER_FILL id=" + std::to_string(o.id) +
                " ts=" + std::to_string(rep.ts) +
                " side=" + (o.side == Side::Buy ? "buy" : "sell") +
                " px=" + std::to_string(rep.avgPrice) +
                " qty=" + std::to_string(rep.filled) +
                (isMaker ? " maker" : " taker"));

    strat_->on_fill(rep, o.side, isMaker, ctx_);
}

void BacktestEngine::recompute_equity(NanoTime ts, double midPx)
{
    const double eq = pf_.cash + pf_.position * midPx;
    if (equity_.empty())
    {
        equity_.push_back(eq);
        equityTs_.push_back(ts);
        inventory_.push_back(pf_.position);
        lastEquity_ = eq;
        unrealRets_.push_back(0.0);
        lastTs_ = ts;
    }
    else
    {
        if (lastTs_ >= 0 && ts > lastTs_)
        {
            sumDtNs_ += static_cast<long double>(ts - lastTs_);
            ++dtSamples_;
            lastTs_ = ts;
        }
        const double ret = (lastEquity_ != 0.0) ? (eq - lastEquity_) / std::abs(lastEquity_) : 0.0;
        unrealRets_.push_back(ret);
        equity_.push_back(eq);
        equityTs_.push_back(ts);
        inventory_.push_back(pf_.position);
        lastEquity_ = eq;
    }
}

double BacktestEngine::estimated_steps_per_year() const
{
    if (dtSamples_ == 0 || sumDtNs_ <= 0.0L)
        return 31'536'000.0;
    const long double avgNs = sumDtNs_ / static_cast<long double>(dtSamples_);
    return static_cast<double>(31'536'000.0L * 1'000'000'000.0L / avgNs);
}

void BacktestEngine::process_live_orders_on_l2()
{
    if (liveOrders_.empty())
        return;
    std::vector<ClOrdId> toErase;
    for (const auto& [id, o] : liveOrders_)
    {
        if (o.type == OrderType::Limit && OrderBook::limit_crossed(o, lastBook_))
        {
            const auto& contra = (o.side == Side::Buy) ? lastBook_.asks : lastBook_.bids;
            const double contraQty = contra.empty() ? o.amount : contra.front().amount;
            const ExecReport rep = OrderBook::passive_fill(o, contraQty, lastBook_.ts);
            apply_fill(o, rep, true);
            toErase.push_back(id);
        }
    }
    for (ClOrdId id : toErase)
        liveOrders_.erase(id);
}

void BacktestEngine::process_live_orders_on_trade(const TradeEvent& t)
{
    if (liveOrders_.empty())
        return;
    std::vector<ClOrdId> toErase;
    for (const auto& [id, o] : liveOrders_)
    {
        if (o.type != OrderType::Limit)
            continue;
        if (o.side == Side::Buy && t.price <= o.price)
        {
            const ExecReport rep = OrderBook::passive_fill(o, t.amount, t.ts);
            apply_fill(o, rep, true);
            toErase.push_back(id);
        }
        else if (o.side == Side::Sell && t.price >= o.price)
        {
            const ExecReport rep = OrderBook::passive_fill(o, t.amount, t.ts);
            apply_fill(o, rep, true);
            toErase.push_back(id);
        }
    }
    for (ClOrdId id : toErase)
        liveOrders_.erase(id);
}

} // namespace cmf
