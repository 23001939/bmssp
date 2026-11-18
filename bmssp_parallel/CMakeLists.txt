cmake_minimum_required(VERSION 3.20)
project(bmssp_parallel CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Required hardware compile flags for 128-bit atomic CAS (cmpxchg16b) and high performance
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-O3 -march=native -mcx16 -Wall -Wextra)
endif()

find_package(OpenMP REQUIRED)

include_directories(
    include
    include/baseline/external
    include/baseline/upstream/include
)

# Core library for infrastructure and baseline
add_library(bmssp_core STATIC
    src/core/graph.cpp
    src/baseline/bmssp_seq.cpp
)
target_link_libraries(bmssp_core PUBLIC OpenMP::OpenMP_CXX)

# Concurrent Data Structure D library (Person 2)
add_library(bmssp_data_structure STATIC
    src/data_structure/d0_deque.cpp
    src/data_structure/d1_skiplist.cpp
    src/data_structure/concurrent_frontier_d.cpp
)
target_link_libraries(bmssp_data_structure PUBLIC bmssp_core OpenMP::OpenMP_CXX)

# Person 1 Comprehensive Verification Test Suite
add_executable(test_person1
    tests/test_person1_comprehensive.cpp
)
target_link_libraries(test_person1 PRIVATE bmssp_core OpenMP::OpenMP_CXX)

# Person 2 Concurrent Frontier D QA Stress Test Suite
add_executable(test_d_stress
    tests/test_d_stress.cpp
)
target_link_libraries(test_d_stress PRIVATE bmssp_data_structure bmssp_core OpenMP::OpenMP_CXX)

# Person 2 Before-vs-After A/B Benchmark (MutexFrontierD vs ConcurrentFrontierD)
add_executable(bench_person2_d
    benchmarks/bench_person2_d.cpp
)
target_link_libraries(bench_person2_d PRIVATE bmssp_data_structure bmssp_core OpenMP::OpenMP_CXX)
