cmake_minimum_required(VERSION 3.22 FATAL_ERROR)

project(hft-market-making VERSION 0.0.1 DESCRIPTION "CMF HFT market-making backtester" LANGUAGES CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(HFT_NO_LOGGING "Compile out Logger::debug output" OFF)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

include(cmake/GlobalSettings.cmake)
include(cmake/ThirdPartyLibs.cmake)
include(cmake/GenerateVersionFile.cmake)

add_subdirectory(src)
add_subdirectory(config)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
