# executable
set(TARGET hft-market-making)
add_executable(${TARGET} main.cpp)
target_link_libraries(${TARGET} PUBLIC
    hft-market-making-common
    hft-market-making-engine
)
