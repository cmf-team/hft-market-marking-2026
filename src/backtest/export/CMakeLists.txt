file(GLOB SRC CONFIGURE_DEPENDS *.cpp)
add_library(hft-market-making-export STATIC ${SRC})
target_include_directories(hft-market-making-export PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(hft-market-making-export PUBLIC hft-market-making-engine)
