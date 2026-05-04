add_subdirectory(common)
add_subdirectory(main)
add_library(bt_core STATIC
    data/csv_trade_loader.cpp
    data/csv_lob_loader.cpp
    data/event_stream.cpp
    book/order_book.cpp
    exec/matcher.cpp
    exec/queue_model.cpp
    exec/latency.cpp
    portfolio/portfolio.cpp
    engine/backtest_engine.cpp
    stats/stats.cpp
    strategies/static_quoter.cpp
    strategies/avellaneda_stoikov_quoter.cpp
    strategies/micro_price_quoter.cpp
)
target_include_directories(bt_core
    PUBLIC  ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(bt_core PUBLIC cxx_std_20)

add_executable(backtester main.cpp)
# main.cpp is the engine driver, not a strategy — it legitimately needs the
# private engine header (which in turn pulls exec/latency.hpp and exec/matcher.hpp).
target_include_directories(backtester PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(backtester PRIVATE bt_core)

add_subdirectory(pointers)
