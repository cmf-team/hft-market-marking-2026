file(GLOB SRC CONFIGURE_DEPENDS *.cpp)
add_library(hft-market-making-parser STATIC ${SRC})
target_include_directories(hft-market-making-parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(hft-market-making-parser PUBLIC hft-market-making-common)
