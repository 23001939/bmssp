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

# Person 1 Comprehensive Verification Test Suite
add_executable(test_person1
    tests/test_person1_comprehensive.cpp
)
target_link_libraries(test_person1 PRIVATE bmssp_core OpenMP::OpenMP_CXX)

include(CTest)
if (BUILD_TESTING)
    find_package(Threads REQUIRED)

    foreach(test_target IN ITEMS verify_correctness test_atomic_state test_gadget_transform)
        add_executable(${test_target} tests/${test_target}.cpp)
        target_link_libraries(${test_target} PRIVATE bmssp_core)
        add_test(NAME ${test_target} COMMAND ${test_target})
    endforeach()

    target_link_libraries(test_atomic_state PRIVATE Threads::Threads)
    set_tests_properties(test_atomic_state PROPERTIES TIMEOUT 30)
endif()
