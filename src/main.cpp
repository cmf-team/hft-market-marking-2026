#include "backtest/csv_data_loader.hpp"
#include "backtest/replay_engine.hpp"
#include "backtest/test_strategy.hpp"
#include <iostream>


int main() {
    try {
        constexpr double engine_speed_multiplier = 0.0;
        // Загрузка данных
        backtest::CsvDataLoader loader("MD/trades.csv");
        const auto& events = loader.load();

        std::cout << "Loaded " << events.size() << " events\n";
        std::cout << "sizeof(MarketEvent): " << sizeof(backtest::MarketEvent) << " bytes\n";
        std::cout << "alignof(MarketEvent): " << alignof(backtest::MarketEvent) << " bytes\n\n";

        // Реплей
        backtest::TestStrategy strategy;
        backtest::ReplayEngine engine(events, strategy, engine_speed_multiplier);
        
        std::cout << "Starting replay...\n";
        engine.run();

    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}