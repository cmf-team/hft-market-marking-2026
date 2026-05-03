set(TARGET hft-market-making-strategy)
file(GLOB SRC CONFIGURE_DEPENDS *.cpp)

add_library(${TARGET} STATIC ${SRC})

# Глушим chatty std::cout-логи NaiveCrossing-стратегии (см.
# simple_crossing_strategy.cpp), иначе full-run зальёт stdout.
target_compile_definitions(${TARGET} PRIVATE SIMPLE_STRATEGY_SILENT)

target_link_libraries(${TARGET} PUBLIC
    hft-market-making-common
)
