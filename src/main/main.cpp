// main function for the hft-market-making app
// please, keep it minimalistic

#include "common/BasicTypes.hpp"

#include <iostream>

using namespace cmf;

int main([[maybe_unused]] int argc, [[maybe_unused]] const char* argv[])
{
    try
    {
        std::cout << "Hell! Oh, world!" << std::endl;
    }
    catch (std::exception& ex)
    {
        std::cerr << "HFT market-making app threw an exception: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
