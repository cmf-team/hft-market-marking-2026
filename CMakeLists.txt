cmake_minimum_required(VERSION 3.22 FATAL_ERROR)

project(hft-market-making VERSION 0.0.1 DESCRIPTION "CMF HFT market-making backtester" LANGUAGES CXX C)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

include(cmake/GlobalSettings.cmake)
include(cmake/GenerateVersionFile.cmake)

# Catch2 is needed only for tests.  Keep it out of the normal MSVC app build,
# otherwise CMake tries to download/build Catch2 even when we only need the exe.
if(BUILD_TESTS)
    include(cmake/ThirdPartyLibs.cmake)
endif()

add_subdirectory(src)
add_subdirectory(config)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
