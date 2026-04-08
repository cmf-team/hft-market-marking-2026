add_library(bt_core STATIC
    data/csv_trade_loader.cpp
    data/csv_lob_loader.cpp
    data/event_stream.cpp
    book/order_book.cpp
    exec/matcher.cpp
    exec/queue_model.cpp
)
target_include_directories(bt_core
    PUBLIC  ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(bt_core PUBLIC cxx_std_20)

add_executable(backtester main.cpp)
target_link_libraries(backtester PRIVATE bt_core)
