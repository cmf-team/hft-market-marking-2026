// main function for the hft-market-making app
// please, keep it minimalistic

#include "Backtester.hpp"

#include <exception>
#include <iostream>

int main(int argc, const char* argv[])
{
    try
    {
        const cmf::BacktestConfig config = cmf::parseBacktestArgs(argc, argv);
        const cmf::BacktestSummary summary = cmf::runBacktest(config);
        std::cout << "Backtest finished. LOB rows=" << summary.lobRows << ", trade rows=" << summary.tradeRows
                  << ", closed trades=" << summary.closedTrades << ", total pnl=" << summary.totalPnl << std::endl;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "HFT market-making backtester threw an exception: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
