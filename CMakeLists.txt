cmake_minimum_required(VERSION 3.25)
project(lob LANGUAGES CXX)

# ---------- compiler settings ----------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# default to an optimised build; override with -DCMAKE_BUILD_TYPE=Debug
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(LOB_BUILD_TESTS "Build the unit tests" ON)
option(LOB_BUILD_BENCHMARKS "Build the benchmarks" ON)

find_package(Threads REQUIRED)

# ---------- core library: everything except main() ----------
add_library(lob_core STATIC
    src/orderBook/OrderBook.cpp
    src/requestProcessor/controller/OrderController.cpp
    src/requestProcessor/dispatcher/RequestDispatcher.cpp
)
target_include_directories(lob_core PUBLIC
    src
    src/entities
    src/utils
    src/logger
    src/orderBook
    src/queue
    src/queue/nspsc
    src/queue/spsc
    src/requestProcessor/controller
    src/requestProcessor/dispatcher
)
target_compile_options(lob_core PUBLIC -Wall -Wextra)
target_link_libraries(lob_core PUBLIC Threads::Threads)

# ---------- the application ----------
add_executable(lob src/Main.cpp)
target_link_libraries(lob PRIVATE lob_core)

# ---------- third-party libraries, downloaded at configure time ----------
include(FetchContent)

if(LOB_BUILD_TESTS)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
        SYSTEM)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    include(GoogleTest)

    foreach(test orderBook/OrderBookTest queue/nspsc/NSpscTest queue/spsc/SpscTest)
        get_filename_component(name ${test} NAME)
        add_executable(${name} tests/${test}.cpp)
        target_link_libraries(${name} PRIVATE lob_core GTest::gtest_main)
        gtest_discover_tests(${name})
    endforeach()
endif()

if(LOB_BUILD_BENCHMARKS)
    FetchContent_Declare(benchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.9.1.tar.gz
        SYSTEM)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)

    # SpscBenchmark (benchmarks/queue/spsc) is kept in the repo but not built
    foreach(bench queue/nspsc/NSpscBenchmark queue/nspsc/NSpscLatencyBenchmark orderBook/OrderBookBenchmark orderBook/OrderBookE2EBenchmark)
        get_filename_component(name ${bench} NAME)
        add_executable(${name} benchmarks/${bench}.cpp)
        target_include_directories(${name} PRIVATE benchmarks benchmarks/orderBook)
        target_link_libraries(${name} PRIVATE lob_core benchmark::benchmark_main)
    endforeach()
endif()
