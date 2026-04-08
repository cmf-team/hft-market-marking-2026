add_library(bt_core INTERFACE)
target_include_directories(bt_core INTERFACE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(bt_core INTERFACE cxx_std_20)

add_executable(backtester main.cpp)
target_link_libraries(backtester PRIVATE bt_core)
