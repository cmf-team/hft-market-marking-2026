set(TARGET hft-market-making-engine)
file(GLOB SRC CONFIGURE_DEPENDS *.cpp)

add_library(${TARGET} STATIC ${SRC})

target_link_libraries(${TARGET} PUBLIC
    hft-market-making-core
    hft-market-making-data
    hft-market-making-book
    hft-market-making-exec
    hft-market-making-strategy
    hft-market-making-analytics
)
