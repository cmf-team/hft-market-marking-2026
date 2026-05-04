#include "factory.hpp"

StrategyVariant make_strategy(StrategyType type, TradeReader& trade_rdr, int64_t qty)
{
    switch (type)
    {
    case StrategyType::Passive:
        return PassiveStrategy{qty};
    case StrategyType::Replay:
        return ReplayStrategy{trade_rdr};
    case StrategyType::TrendFollowing:
        return TrendFollowingStrategy{qty};
    case StrategyType::AvellanedaStoikovStrategy:
        return AvellanedaStoikovStrategy{qty};
    case StrategyType::MicropriceAvellanedaStoikovStrategy:
        return MicropriceAvellanedaStoikovStrategy{qty};
    }
    __builtin_unreachable();
}

std::string_view strategy_name(StrategyType type) noexcept
{
    switch (type)
    {
    case StrategyType::Passive:
        return PassiveStrategy::name();
    case StrategyType::Replay:
        return ReplayStrategy::name();
    case StrategyType::TrendFollowing:
        return TrendFollowingStrategy::name();
    case StrategyType::AvellanedaStoikovStrategy:
        return AvellanedaStoikovStrategy::name();
    case StrategyType::MicropriceAvellanedaStoikovStrategy:
        return MicropriceAvellanedaStoikovStrategy::name();
    }
    __builtin_unreachable();
}
