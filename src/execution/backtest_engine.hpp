#pragma once

#include "data_loader/event_source.hpp"
#include "execution/matching_engine.hpp"
#include "logging.hpp"
#include "portfolio/portfolio.hpp"
#include "strategy/strategy_base.hpp"
#include "strategy/strategy_context.hpp"
#include "types.hpp"

#include <optional>
#include <type_traits>
#include <variant>

namespace hft::execution {

/**
 * @brief Stores switches that control matching and marking during a backtest.
 */
struct BacktestConfig {
  bool match_on_trade = true;
  bool match_on_book_cross = false;
  bool mark_to_market_on_book = true;
  bool mark_to_market_on_trade = false;
};

/**
 * @brief Runs a strategy over market events with matching and portfolio updates.
 */
class BacktestEngine {
public:
  /**
   * @brief Creates a backtest engine with default state.
   */
  BacktestEngine() = default;

  /**
   * @brief Sets the portfolio used by future runs.
   * @param p Portfolio shared pointer to store.
   */
  void setPortfolio(portfolio::Portfolio::SPtr p);

  /**
   * @brief Sets matching and marking behavior.
   * @param cfg Input backtest configuration.
   */
  void setConfig(const BacktestConfig &cfg);

  /**
   * @brief Runs a strategy over all events from a source.
   * @param src Input market event source.
   * @param strategy Strategy instance receiving callbacks.
   * @return True when the run starts and completes.
   */
  template <data::EventSource Src, strategy::Strategy StrategyT>
  bool run(Src &src, StrategyT &strategy) {
    if (!beginRun(strategy))
      return false;
    while (src.hasNext()) {
      handleEvent(src.next(), strategy);
    }
    endRun(strategy);
    return true;
  }

  /**
   * @brief Returns the engine portfolio.
   * @return Reference to the configured portfolio.
   */
  const portfolio::Portfolio &portfolio() const;

  /**
   * @brief Returns the matching engine.
   * @return Reference to the internal matching engine.
   */
  const MatchingEngine &matchingEngine() const;

  /**
   * @brief Returns the active backtest configuration.
   * @return Reference to the current configuration.
   */
  const BacktestConfig &config() const;

private:
  /**
   * @brief Calculates the mid price from a book snapshot.
   * @param book Input book snapshot.
   * @return Mid price, or zero when either side is empty.
   */
  static double midFrom(const LOBData &book) {
    if (book.bids.empty() || book.asks.empty())
      return 0.0;
    return 0.5 * (book.bids.front().price + book.asks.front().price);
  }

  /**
   * @brief Initializes per-run state and notifies the strategy.
   * @param strategy Strategy instance receiving the start callback.
   * @return True when the run can proceed.
   */
  template <strategy::Strategy StrategyT> bool beginRun(StrategyT &strategy) {
    if (!portfolio_) {
      logging::Logger::debug("[ENGINE] no portfolio set");
      return false;
    }

    strategy_context_.emplace(&matching_, portfolio_.get());
    strategy_context_->setFillSink(
        [this, &strategy](const Fill &fill, strategy::StrategyContext &ctx) {
          portfolio_->applyFill(fill);
          strategy::callOnFill(strategy, fill, ctx);
        });
    have_book_ = false;
    last_book_ = {};
    strategy::callOnStart(strategy, *strategy_context_);
    return true;
  }

  /**
   * @brief Finalizes per-run state and notifies the strategy.
   * @param strategy Strategy instance receiving the finish callback.
   */
  template <strategy::Strategy StrategyT> void endRun(StrategyT &strategy) {
    if (strategy_context_) {
      strategy::callOnFinish(strategy, *strategy_context_);
      if (have_book_ && config_.mark_to_market_on_book) {
        const double mid_price = midFrom(last_book_);
        if (mid_price > 0.0)
          portfolio_->mark(mid_price, last_book_.ts);
      }
      strategy_context_.reset();
    }
  }

  /**
   * @brief Dispatches one market event to the matching and strategy flow.
   * @param market_event Input market event.
   * @param strategy Strategy instance receiving callbacks.
   */
  template <strategy::Strategy StrategyT>
  void handleEvent(MarketEvent market_event, StrategyT &strategy) {
    std::visit(
        [&](auto &payload) {
          using T = std::decay_t<decltype(payload)>;
          strategy_context_->setNow(payload.ts);

          if constexpr (std::is_same_v<T, LOBData>) {
            handleLobEvent(payload, strategy);
          } else if constexpr (std::is_same_v<T, TradeData>) {
            handleTradeEvent(payload, strategy);
          }
        },
        market_event);
  }

  /**
   * @brief Handles one book snapshot event.
   * @param lob_book Input book snapshot.
   * @param strategy Strategy instance receiving callbacks.
   */
  template <strategy::Strategy StrategyT>
  void handleLobEvent(LOBData &lob_book, StrategyT &strategy) {
    last_book_ = std::move(lob_book);
    have_book_ = true;
    strategy_context_->setBook(last_book_);

    if (config_.match_on_book_cross) {
      auto fills = matching_.onBookUpdate(last_book_);
      for (const auto &fill : fills) {
        portfolio_->applyFill(fill);
        strategy::callOnFill(strategy, fill, *strategy_context_);
      }
    }

    if (config_.mark_to_market_on_book) {
      const double mid_price = midFrom(last_book_);
      if (mid_price > 0.0)
        portfolio_->mark(mid_price, last_book_.ts);
    }

    strategy.onMarketData(last_book_, *strategy_context_);
  }

  /**
   * @brief Handles one trade print event.
   * @param trade Input trade print.
   * @param strategy Strategy instance receiving callbacks.
   */
  template <strategy::Strategy StrategyT>
  void handleTradeEvent(TradeData &trade, StrategyT &strategy) {
    if (have_book_)
      strategy_context_->setBook(last_book_);

    if (config_.match_on_trade) {
      auto fills = matching_.onTrade(trade);
      for (const auto &fill : fills) {
        portfolio_->applyFill(fill);
        strategy::callOnFill(strategy, fill, *strategy_context_);
      }
    }

    if (config_.mark_to_market_on_trade && have_book_) {
      const double mid_price = midFrom(last_book_);
      if (mid_price > 0.0)
        portfolio_->mark(mid_price, trade.ts);
    }

    strategy::callOnTrade(strategy, trade, *strategy_context_);
  }

  std::optional<strategy::StrategyContext> strategy_context_;
  bool have_book_{false};
  LOBData last_book_{};

  BacktestConfig config_;
  portfolio::Portfolio::SPtr portfolio_;
  MatchingEngine matching_;
};

}
