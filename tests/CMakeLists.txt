include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# Don't install gtest with the project.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(bt_tests
    test_types.cpp
    test_csv_trade_loader.cpp
    test_csv_lob_loader.cpp
    test_event_stream.cpp
)
target_include_directories(bt_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bt_tests PRIVATE bt_core GTest::gtest_main)

gtest_discover_tests(bt_tests)
