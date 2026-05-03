// main function for the hft-market-making app
// please, keep it minimalistic

#include "main/Backtester.hpp"

#include <exception>
#include <iostream>

using namespace cmf;

int main(int argc, const char* argv[])
{
    try
    {
        return runBacktest(parseArgs(argc, argv));
    }
    catch (std::exception& ex)
    {
        std::cerr << "HFT market-making app threw an exception: " << ex.what() << std::endl;
        return 1;
    }
}
