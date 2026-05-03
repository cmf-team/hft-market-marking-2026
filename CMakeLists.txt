cmake_minimum_required(VERSION 3.20)
project(hft-backtest-engine VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # Для clangd/LSP


option(BUILD_TESTS      "Build unit tests with Catch2"      ON)
option(BUILD_BENCHMARKS "Build benchmarks with Google Benchmark" OFF)

add_executable(hft-backtest-engine
        src/main.cpp
        src/csv_data_loader.cpp
        src/strategy_avellaneda.cpp
        src/strategy_microprice.cpp
)

target_include_directories(hft-backtest-engine
        PRIVATE ${CMAKE_SOURCE_DIR}/include
)

target_compile_options(hft-backtest-engine PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnull-dereference
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Debug build: sanitizers enabled")

    target_compile_options(hft-backtest-engine PRIVATE
            -O0 -g3 -fsanitize=address,undefined -DDEBUG
    )
    target_link_options(hft-backtest-engine PRIVATE
            -fsanitize=address,undefined
    )

elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Release build: optimizations enabled")

    target_compile_options(hft-backtest-engine PRIVATE
            -O3 -march=native
    )
    target_compile_definitions(hft-backtest-engine PRIVATE NDEBUG)

    # Межпроцедурная оптимизация (LTO) если поддерживается
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
        set_property(TARGET hft-backtest-engine PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        message(STATUS "LTO enabled")
    else()
        message(STATUS "LTO not supported: ${ipo_error}")
    endif()

elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    message(STATUS "⚖RelWithDebInfo build: optimizations + debug symbols")

    target_compile_options(hft-backtest-engine PRIVATE
            -O2 -g -march=native
    )
endif()

if(BUILD_TESTS)
    message(STATUS "Unit tests enabled")
    add_subdirectory(tests)
endif()

if(BUILD_BENCHMARKS)
    message(STATUS "Benchmarks enabled")
    add_subdirectory(benchmarks)
endif()