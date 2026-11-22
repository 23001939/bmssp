cmake_minimum_required(VERSION 3.20)
project(bmssp_parallel CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BMSSP_ENABLE_TSAN "Build with ThreadSanitizer" OFF)
option(BMSSP_ENABLE_PROFILING "Collect BMSSP phase counters" OFF)

# Required hardware compile flags for 128-bit atomic CAS (cmpxchg16b) and high performance
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-march=native -mcx16 -Wall -Wextra)
    if (BMSSP_ENABLE_TSAN)
        add_compile_options(-fsanitize=thread -g -O1)
        add_link_options(-fsanitize=thread)
    else()
        add_compile_options(-O3)
    endif()
elseif (BMSSP_ENABLE_TSAN)
    message(FATAL_ERROR "ThreadSanitizer requires GCC or Clang")
endif()

find_package(OpenMP REQUIRED)
if (BMSSP_ENABLE_PROFILING)
    add_compile_definitions(BMSSP_ENABLE_PROFILING)
endif()

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
if (BMSSP_ENABLE_TSAN)
    target_compile_definitions(test_d_stress PRIVATE BMSSP_TSAN_TEST_RUN)
endif()

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

add_executable(bench_synthetic benchmarks/bench_synthetic.cpp)
target_link_libraries(bench_synthetic PRIVATE bmssp_parallel_lib)

enable_testing()
add_test(NAME test_person1 COMMAND test_person1)
add_test(NAME test_d_stress COMMAND test_d_stress)
set_tests_properties(test_d_stress PROPERTIES TIMEOUT 180)
add_test(NAME verify_parallel_bmssp COMMAND verify_parallel_bmssp)
add_test(NAME test_parallel_edge_cases COMMAND test_parallel_edge_cases)

include(CTest)
if (BUILD_TESTING)
    find_package(Threads REQUIRED)

    foreach(test_target IN ITEMS verify_correctness test_atomic_state test_gadget_transform)
        add_executable(${test_target} tests/${test_target}.cpp)
        target_link_libraries(${test_target} PRIVATE bmssp_core)
        add_test(NAME ${test_target} COMMAND ${test_target})
    endforeach()

    target_link_libraries(verify_correctness PRIVATE bmssp_parallel_lib)
    target_include_directories(verify_correctness PRIVATE src/algorithm)
    target_link_libraries(test_atomic_state PRIVATE Threads::Threads)
    set_tests_properties(test_atomic_state PROPERTIES TIMEOUT 30)
    add_test(NAME verify_correctness_concurrent COMMAND verify_correctness --frontier=concurrent)
    set_tests_properties(verify_correctness verify_correctness_concurrent PROPERTIES TIMEOUT 120)
endif()
