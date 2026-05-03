#pragma once
#include "common/EventTypes.hpp"
#include "common/LimitOrderBook.hpp"
#include "common/KalmanFilter.hpp"
#include "common/Strategy.hpp"
#include "common/CsvReader.hpp"
#include <vector>

namespace cmf {

struct FillRecord {
    NanoTime time;
    Side side;
    Price price;
    Quantity qty;
};

struct SimResult {
    double finalPnL = 0;
    Quantity finalInventory = 0;
    std::vector<FillRecord> fills;
    std::vector<double> inventoryPath;
    std::vector<double> pnlPath;
};

class SimulationEngine {
public:
    SimulationEngine(const std::string& lobFile,
                     const std::string& tradeFile,
                     const StratParams& params = {});

    SimResult run();

private:
    AvellanedaStoikov strategy_;
    std::vector<Event> events_;
    bool hasQuote_ = false;
    AvellanedaStoikov::Quote currentQuote_;   // always initialized when hasQuote_ == true

    void loadEvents(const std::string& lobFile, const std::string& tradeFile);
};

} // namespace cmf