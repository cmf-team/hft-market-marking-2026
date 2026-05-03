#include "SimulationEngine.hpp"
#include <iostream>
#include <fstream>

using namespace cmf;

int main(int argc, const char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <lob.csv> <trades.csv> [gamma=0.01 [sigma=0.02 [T=300]]]\n";
        return 1;
    }
    std::string lobFile = argv[1];
    std::string tradeFile = argv[2];

    StratParams params;
    if (argc > 3) params.gamma = std::stod(argv[3]);
    if (argc > 4) params.sigma = std::stod(argv[4]);
    if (argc > 5) params.timeHorizonSec = std::stod(argv[5]);

    SimulationEngine engine(lobFile, tradeFile, params);
    auto result = engine.run();

    std::cout << "Final PnL: " << result.finalPnL << '\n';
    std::cout << "Final Inventory: " << result.finalInventory << '\n';
    std::cout << "Number of fills: " << result.fills.size() << '\n';

    // Save paths for plotting
    std::ofstream f("result.csv");
    f << "inventory,pnl\n";
    for (size_t i = 0; i < result.inventoryPath.size(); ++i)
        f << result.inventoryPath[i] << ',' << result.pnlPath[i] << '\n';

    return 0;
}