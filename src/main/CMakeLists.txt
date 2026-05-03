set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(TARGET hft-market-making)
add_executable(${TARGET} main.cpp)
target_link_libraries(${TARGET} PRIVATE hft-market-making-strategy)
