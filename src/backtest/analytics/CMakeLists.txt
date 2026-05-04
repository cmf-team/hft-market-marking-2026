file(GLOB SRC CONFIGURE_DEPENDS *.cpp)
add_library(hft-market-making-analytics STATIC ${SRC})
target_include_directories(hft-market-making-analytics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(hft-market-making-analytics PUBLIC hft-market-making-engine)
