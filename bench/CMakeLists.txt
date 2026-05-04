set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/bench)

file(GLOB BENCH_SRC CONFIGURE_DEPENDS *.cpp)

set(TARGET hft-market-making-benchmarks)
add_executable(${TARGET} ${BENCH_SRC})

target_link_libraries(${TARGET} PRIVATE
    benchmark-static-lib
    hft-market-making-analytics
    hft-market-making-export
    hft-market-making-engine
    hft-market-making-strategies
    hft-market-making-order-book
    hft-market-making-parser
    hft-market-making-common
)
