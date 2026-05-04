#pragma once
#include "backtest/strategies/AvellanedaStoikovStrategy.hpp"
#include "backtest/strategies/MicropriceAvellanedaStoikovStrategy.hpp"
#include "backtest/strategies/PassiveStrategy.hpp"
#include "backtest/strategies/ReplayStrategy.hpp"
#include "backtest/strategies/TrendFollowingStrategy.hpp"
#include <variant>

enum class StrategyType
{
    Passive,
    Replay,
    TrendFollowing,
    AvellanedaStoikovStrategy,
    MicropriceAvellanedaStoikovStrategy
};

constexpr std::array ALL_STRATEGIES = {
    StrategyType::MicropriceAvellanedaStoikovStrategy,
    StrategyType::AvellanedaStoikovStrategy,
    StrategyType::Passive,
    StrategyType::Replay,
    StrategyType::TrendFollowing,
};

using StrategyVariant = std::variant<PassiveStrategy, ReplayStrategy, TrendFollowingStrategy, AvellanedaStoikovStrategy, MicropriceAvellanedaStoikovStrategy>;

StrategyVariant make_strategy(StrategyType type, TradeReader& trade_rdr, int64_t qty = 1'000);
std::string_view strategy_name(StrategyType type) noexcept;
