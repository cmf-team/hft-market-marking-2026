# Library sources from this folder, excluding main.cpp.
# In the original GCC-first template, list(REMOVE_ITEM SRC main.cpp) did not remove
# the file because GLOB returns absolute paths.  On a clean repo this made main.cpp
# part of the static library too.  Use the absolute path below.
file(GLOB SRC CONFIGURE_DEPENDS "*.cpp")
list(REMOVE_ITEM SRC "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")

set(TARGET hft-market-making-lib)

# The original project currently has only main.cpp in src/main.  CMake cannot create
# a STATIC library with zero source files, so create an INTERFACE target until real
# strategy/order-book source files are added here.
if(SRC)
    add_library(${TARGET} STATIC ${SRC})
    target_link_libraries(${TARGET} PUBLIC
        hft-market-making-common
    )
else()
    add_library(${TARGET} INTERFACE)
    target_link_libraries(${TARGET} INTERFACE
        hft-market-making-common
    )
endif()

set(TARGET hft-market-making)
add_executable(${TARGET} main.cpp)
target_link_libraries(${TARGET} PUBLIC hft-market-making-lib)
