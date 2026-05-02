#pragma once

#include "strategy/avellaneda_stoikov_strategy.hpp"
#include "backtest/backtest_data_reader.hpp"
#include "backtest/execution_simulator.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace hft_backtest {

enum class StrategyKind {
    Naive,                      // старая SimpleCrossingStrategy
    AvellanedaStoikov2008,      // базовый AS
    AvellanedaStoikov2018Micro  // AS на microprice
};

struct BacktestConfig {
    // Источники данных.
    std::string lob_path    = "../MD/lob.csv";
    std::string trades_path = "../MD/trades.csv";

    // Оптимизация: trades.csv нужен только если стратегия слушает trade-tape.
    // AS-стратегии работают исключительно по lob, поэтому можно сэкономить
    // ~5 минут парсинга. По умолчанию -- грузим, чтобы не сломать совместимость.
    bool load_trades = true;

    // Какую стратегию запускаем.
    StrategyKind strategy = StrategyKind::AvellanedaStoikov2008;

    // Общая часть.
    double   initial_cash         = 1'000'000.0;
    uint64_t start_time_us        = 0;
    uint64_t end_time_us          = UINT64_MAX;
    uint64_t max_events           = 0;       // 0 = без ограничения
    uint64_t progress_interval    = 100'000;
    bool     print_progress       = true;

    // Исполнение.
    double transaction_cost_bps = 1.0;
    double slippage_bps         = 0.0;
    bool   queue_priority       = true;

    // Avellaneda-Stoikov.
    AvellanedaStoikovConfig as_cfg{};

    // Куда писать отчёты.
    std::string summary_csv     = "backtest_summary.csv";
    std::string timeseries_csv  = "backtest_timeseries.csv";
    uint64_t    timeseries_step = 1000;  // писать каждую N-ю запись
};

// Загрузить config из простого ini-формата:
//   key = value
//   ; и # -- комментарии
//   секции в квадратных скобках игнорируются (используем плоские ключи).
//
// Поддерживаемые ключи (см. apply()).
class ConfigLoader {
public:
    static bool load(const std::string& path, BacktestConfig& cfg);
};

}  // namespace hft_backtest
