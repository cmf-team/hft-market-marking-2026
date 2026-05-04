file(GLOB SRC CONFIGURE_DEPENDS *.cpp)
add_library(hft-market-making-order-book STATIC ${SRC})
target_include_directories(hft-market-making-order-book PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(hft-market-making-order-book PUBLIC hft-market-making-common)
