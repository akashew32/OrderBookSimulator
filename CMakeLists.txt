cmake_minimum_required(VERSION 3.16)
project(LimitOrderBookSimulator)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
enable_testing()

include_directories(include)
include_directories(benchmarks)

add_executable(limit_order_book
    src/main.cpp
    src/order_book.cpp
    src/matching_engine.cpp
    src/simulator.cpp
    src/metrics.cpp
)

add_executable(live_order_book_server
    src/live_server.cpp
    src/csv_replay.cpp
    src/csv_files.cpp
    src/event_queue.cpp
    src/optimizer.cpp
    src/strategy.cpp
    src/order_book.cpp
    src/matching_engine.cpp
    src/metrics.cpp
)

add_executable(test_csv_replay
    tests/test_csv_replay.cpp
    src/csv_replay.cpp
)

add_executable(test_order_book
    tests/test_order_book.cpp
    src/order_book.cpp
)

add_executable(test_matching_engine
    tests/test_matching_engine.cpp
    src/matching_engine.cpp
    src/order_book.cpp
    src/metrics.cpp
)

add_executable(test_strategy
    tests/test_strategy.cpp
    src/strategy.cpp
    src/order_book.cpp
)

add_executable(test_order_lifecycle
    tests/test_order_lifecycle.cpp
    src/matching_engine.cpp
    src/order_book.cpp
    src/metrics.cpp
)

add_executable(test_latency
    tests/test_latency.cpp
    src/event_queue.cpp
    src/matching_engine.cpp
    src/order_book.cpp
    src/metrics.cpp
)

add_executable(validate_replay_file
    tests/validate_replay_file.cpp
    src/csv_replay.cpp
    src/matching_engine.cpp
    src/order_book.cpp
    src/metrics.cpp
)

add_executable(test_csv_files
    tests/test_csv_files.cpp
    src/csv_files.cpp
    src/csv_replay.cpp
)

add_executable(test_optimizer
    tests/test_optimizer.cpp
    src/optimizer.cpp
    src/csv_replay.cpp
)

add_executable(test_ostep_systems
    tests/test_ostep_systems.cpp
    src/event_queue.cpp
)

add_executable(order_book_benchmark
    benchmarks/order_book_benchmark.cpp
    src/matching_engine.cpp
    src/order_book.cpp
    src/metrics.cpp
)

add_executable(test_benchmark_suite
    tests/test_benchmark_suite.cpp
    src/matching_engine.cpp
    src/order_book.cpp
    src/metrics.cpp
)

add_test(NAME test_csv_replay COMMAND test_csv_replay)
add_test(NAME test_order_book COMMAND test_order_book)
add_test(NAME test_matching_engine COMMAND test_matching_engine)
add_test(NAME test_strategy COMMAND test_strategy)
add_test(NAME test_order_lifecycle COMMAND test_order_lifecycle)
add_test(NAME test_latency COMMAND test_latency)
add_test(NAME test_csv_files COMMAND test_csv_files)
add_test(NAME test_optimizer COMMAND test_optimizer)
add_test(NAME test_ostep_systems COMMAND test_ostep_systems)
add_test(NAME test_benchmark_suite COMMAND test_benchmark_suite)
