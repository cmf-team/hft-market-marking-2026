cmake_minimum_required(VERSION 3.22)
project(backtester CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)

include(CTest)
enable_testing()

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
