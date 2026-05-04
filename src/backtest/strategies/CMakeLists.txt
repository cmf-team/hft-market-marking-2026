file(GLOB SRC CONFIGURE_DEPENDS *.cpp)
add_library(hft-market-making-strategies STATIC ${SRC})
target_include_directories(hft-market-making-strategies PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(hft-market-making-strategies PUBLIC
    hft-market-making-order-book
    hft-market-making-parser
)
