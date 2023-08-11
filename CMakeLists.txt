cmake_minimum_required(VERSION 3.13)

project(dynamic_rc_benchmark CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(benchmark REQUIRED)

add_executable(dynamic_rc_benchmark src/dynamic_rc_benchmark.cpp src/cycle_collector.cpp)

target_compile_options(dynamic_rc_benchmark PUBLIC -O3 -Wall -fstack-protector)

target_link_libraries(dynamic_rc_benchmark benchmark::benchmark)
