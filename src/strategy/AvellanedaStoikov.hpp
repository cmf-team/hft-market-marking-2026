#pragma once

#include "engine/Engine.hpp"
#include "strategy/Microprice.hpp"
#include "strategy/Volatility.hpp"
#include <cmath>

namespace cmf
{

enum class RefPriceMode
{
    Mid,
    Microprice
};

struct AvellanedaStoikovMM : Strategy
{
    RefPriceMode mode;
    double gamma;
    double k;
    double sessionTicks;
    double invCap;
    double baseQty;
    EwmaVariance vol;
    double position{0.0};
    double tickCount{0.0};
    ClOrdId bidId{0};
    ClOrdId askId{0};

    AvellanedaStoikovMM(RefPriceMode m, double g, double k_,
                        double sessionT, double cap, double qty, double halfLife)
        : mode(m), gamma(g), k(k_), sessionTicks(sessionT),
          invCap(cap), baseQty(qty), vol(halfLife)
    {
    }

    void on_l2(const L2Snapshot& l2, StrategyContext& ctx) override
    {
        if (l2.asks.empty() || l2.bids.empty())
            return;
        const double s = (mode == RefPriceMode::Microprice) ? microprice(l2) : mid(l2);
        vol.update(s);
        tickCount += 1.0;
        if (!vol.initialized())
            return;

        const double sigma2 = vol.value();
        const double tRemaining = std::max(0.0, sessionTicks - tickCount);
        const double r = s - position * gamma * sigma2 * tRemaining;
        const double halfSpread = 0.5 * gamma * sigma2 * tRemaining
                                  + (1.0 / gamma) * std::log1p(gamma / k);
        if (halfSpread <= 0.0 || !std::isfinite(r) || !std::isfinite(halfSpread))
            return;

        if (bidId)
        {
            ctx.cancel_order(bidId);
            bidId = 0;
        }
        if (askId)
        {
            ctx.cancel_order(askId);
            askId = 0;
        }

        if (position < invCap)
        {
            Order ob{0, OrderType::Limit, Side::Buy, r - halfSpread, baseQty, l2.ts};
            bidId = ctx.send_order(ob);
        }
        if (position > -invCap)
        {
            Order oa{0, OrderType::Limit, Side::Sell, r + halfSpread, baseQty, l2.ts};
            askId = ctx.send_order(oa);
        }
    }

    void on_fill(const ExecReport& rep, Side side, bool, StrategyContext&) override
    {
        position += (side == Side::Buy ? 1.0 : -1.0) * rep.filled;
        if (side == Side::Buy && rep.orderId == bidId)
            bidId = 0;
        if (side == Side::Sell && rep.orderId == askId)
            askId = 0;
    }

    void on_trade(const TradeEvent&, StrategyContext&) override {}
};

} // namespace cmf
