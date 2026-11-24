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

# Core library for infrastructure and sequential baseline
add_library(bmssp_core STATIC
    src/core/graph.cpp
    src/baseline/bmssp_seq.cpp
)
target_link_libraries(bmssp_core PUBLIC OpenMP::OpenMP_CXX)

# Concurrent tiered frontier data structure (Lemma 3.3)
add_library(bmssp_data_structure STATIC
    src/data_structure/d0_deque.cpp
    src/data_structure/d1_skiplist.cpp
    src/data_structure/concurrent_frontier_d.cpp
)
target_link_libraries(bmssp_data_structure PUBLIC bmssp_core OpenMP::OpenMP_CXX)

# Parallel BMSSP algorithm library
add_library(bmssp_parallel_lib STATIC
    src/algorithm/find_pivots.cpp
    src/algorithm/base_case.cpp
    src/algorithm/bmssp.cpp
)
target_link_libraries(bmssp_parallel_lib PUBLIC bmssp_data_structure bmssp_core OpenMP::OpenMP_CXX)

# Test suites
add_executable(test_person1
    tests/test_person1_comprehensive.cpp
)
target_link_libraries(test_person1 PRIVATE bmssp_core OpenMP::OpenMP_CXX)

add_executable(test_d_stress
    tests/test_d_stress.cpp
)
target_link_libraries(test_d_stress PRIVATE bmssp_data_structure bmssp_core OpenMP::OpenMP_CXX)

add_executable(verify_parallel_bmssp
    tests/verify_parallel_bmssp.cpp
)
target_link_libraries(verify_parallel_bmssp PRIVATE bmssp_parallel_lib OpenMP::OpenMP_CXX)

add_executable(test_parallel_edge_cases
    tests/test_parallel_edge_cases.cpp
)
target_link_libraries(test_parallel_edge_cases PRIVATE bmssp_parallel_lib OpenMP::OpenMP_CXX)

# Benchmarks
add_executable(bench_person2_d
    benchmarks/bench_person2_d.cpp
)
target_link_libraries(bench_person2_d PRIVATE bmssp_data_structure bmssp_core OpenMP::OpenMP_CXX)

add_executable(bench_parallel_bmssp
    benchmarks/bench_parallel_bmssp.cpp
)
target_link_libraries(bench_parallel_bmssp PRIVATE bmssp_parallel_lib OpenMP::OpenMP_CXX)

add_executable(bench_final_vs_dijkstra
    benchmarks/bench_final_vs_dijkstra.cpp
)
target_link_libraries(bench_final_vs_dijkstra PRIVATE bmssp_parallel_lib OpenMP::OpenMP_CXX)

add_executable(bench_dimacs_road
    benchmarks/bench_dimacs_road.cpp
)
target_link_libraries(bench_dimacs_road PRIVATE bmssp_parallel_lib OpenMP::OpenMP_CXX)

add_executable(bench_snap_highdeg
    benchmarks/bench_snap_highdeg.cpp
)
target_link_libraries(bench_snap_highdeg PRIVATE bmssp_parallel_lib OpenMP::OpenMP_CXX)

enable_testing()
add_test(NAME test_person1 COMMAND test_person1)
add_test(NAME test_d_stress COMMAND test_d_stress)
add_test(NAME verify_parallel_bmssp COMMAND verify_parallel_bmssp)
add_test(NAME test_parallel_edge_cases COMMAND test_parallel_edge_cases)
