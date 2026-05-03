# executable: thin wrapper around backtest+strategy libs.
set(TARGET hft-market-making)
add_executable(${TARGET} main.cpp)

target_link_libraries(${TARGET} PUBLIC
    hft-market-making-backtest
    hft-market-making-strategy
    hft-market-making-common
)
