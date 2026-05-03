set(TARGET hft-market-making-io)
file(GLOB SRC CONFIGURE_DEPENDS *.cpp)
add_library(${TARGET} STATIC ${SRC})
target_link_libraries(${TARGET} PUBLIC hft-market-making-engine)
