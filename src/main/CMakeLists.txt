file(GLOB SRC CONFIGURE_DEPENDS *.cpp)
list(REMOVE_ITEM SRC "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")

# static library
set(TARGET hft-market-making-lib)
add_library(${TARGET} STATIC ${SRC})
target_link_libraries(${TARGET} PUBLIC
    hft-market-making-common
    hft-market-making-analytics
    hft-market-making-export
)

# executable
set(TARGET hft-market-making)
add_executable(${TARGET} main.cpp)
target_link_libraries(${TARGET} PUBLIC hft-market-making-lib)
