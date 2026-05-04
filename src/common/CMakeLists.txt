set(TARGET hft-market-making-common)
file(GLOB SRC CONFIGURE_DEPENDS *.cpp)

add_library(${TARGET} STATIC ${SRC})
add_dependencies(${TARGET} GenerateVersion)

target_link_libraries(${TARGET} PUBLIC
)
