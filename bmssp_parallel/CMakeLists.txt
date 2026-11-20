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
    src/baseline/dijkstra_openmp.cpp
)
target_link_libraries(bmssp_core PUBLIC OpenMP::OpenMP_CXX)

# Person 1 Comprehensive Verification Test Suite
add_executable(test_person1
    tests/test_person1_comprehensive.cpp
)
target_link_libraries(test_person1 PRIVATE bmssp_core OpenMP::OpenMP_CXX)

enable_testing()
add_executable(openmp_runtime_probe tests/openmp_runtime_probe.cpp)
target_link_libraries(openmp_runtime_probe PRIVATE OpenMP::OpenMP_CXX)
add_test(NAME openmp_runtime COMMAND openmp_runtime_probe)
add_test(NAME person1 COMMAND test_person1)
add_executable(test_dijkstra_openmp tests/test_dijkstra_openmp.cpp)
target_link_libraries(test_dijkstra_openmp PRIVATE bmssp_core)
add_test(NAME dijkstra_openmp COMMAND test_dijkstra_openmp)
add_executable(bench_dijkstra_openmp benchmarks/bench_dijkstra_openmp.cpp)
target_link_libraries(bench_dijkstra_openmp PRIVATE bmssp_core)
add_executable(demo_dijkstra_openmp examples/demo_dijkstra_openmp.cpp)
target_link_libraries(demo_dijkstra_openmp PRIVATE bmssp_core)
set_tests_properties(person1 dijkstra_openmp openmp_runtime PROPERTIES
    TIMEOUT 180
    ENVIRONMENT "OMP_NUM_THREADS=4;OMP_DYNAMIC=FALSE")
