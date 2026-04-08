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
    test_order_book.cpp
    test_matcher.cpp
)
# tests/ for shared fixtures, src/ so tests can reach internal headers like
# exec/matcher.hpp that are deliberately not exposed via include/bt/.
target_include_directories(bt_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(bt_tests PRIVATE bt_core GTest::gtest_main)

gtest_discover_tests(bt_tests)
