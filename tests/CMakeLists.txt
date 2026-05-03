include(FetchContent)

FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.4.0
        GIT_SHALLOW    ON
)

FetchContent_MakeAvailable(Catch2)

add_executable(hft-tests
        main.cpp
        test_execution.cpp
        test_ofi.cpp
        test_replay.cpp
)

target_link_libraries(hft-tests PRIVATE
        Catch2::Catch2WithMain
)

target_include_directories(hft-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${catch2_SOURCE_DIR}/src
        ${catch2_BINARY_DIR}/generated-includes
)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

include(CTest)
add_test(
        NAME HFT-Tests
        COMMAND hft-tests
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_compile_options(hft-tests PRIVATE
        $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(hft-tests PRIVATE
            -fsanitize=address,undefined
    )
    target_link_options(hft-tests PRIVATE
            -fsanitize=address,undefined
    )
endif()