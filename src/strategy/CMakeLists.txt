set(TARGET hft-market-making-strategy)
file(GLOB SRC CONFIGURE_DEPENDS *.cpp strategies/*.cpp)

add_library(${TARGET} STATIC ${SRC})

target_link_libraries(${TARGET} PUBLIC
    hft-market-making-core
    hft-market-making-book
    hft-market-making-exec
)
