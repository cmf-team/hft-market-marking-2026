file(GLOB SRC CONFIGURE_DEPENDS *.cpp)
list(REMOVE_ITEM SRC main.cpp)

# static library
set(TARGET hft-market-making-lib)
add_library(${TARGET} STATIC ${SRC})
target_link_libraries(${TARGET} PUBLIC
    hft-market-making-common
)

# executable
set(TARGET hft-market-making)
add_executable(${TARGET} main.cpp)
target_link_libraries(${TARGET} PUBLIC hft-market-making-lib)
