#pragma once

#include <cstddef>

#include "config.hpp"
#include "csv_reader.hpp"
#include "exchange.hpp"
#include "stats.hpp"
#include "strategy.hpp"

namespace hft {


class ReplayEngine {
   public:
    ReplayEngine(const BacktestConfig& config, IStrategy& strategy,
                 CsvLobReader& lob_reader, CsvTradeReader& trade_reader,
                 ExchangeEmulator& exchange, BacktestStats& stats);

    void run();

   private:

    StrategyContext make_context(Timestamp now) const;
    void maybe_sleep(Timestamp next_ts);
    void handle_fills(const std::vector<Fill>& fills);
    void apply_actions(const StrategyActions& actions, Timestamp now);

    const BacktestConfig& config_;
    IStrategy& strategy_;
    CsvLobReader& lob_reader_;
    CsvTradeReader& trade_reader_;
    ExchangeEmulator& exchange_;
    BacktestStats& stats_;

    std::size_t lob_processed_ = 0;
    std::size_t trade_processed_ = 0;
    Timestamp previous_ts_ = -1;
};

}
