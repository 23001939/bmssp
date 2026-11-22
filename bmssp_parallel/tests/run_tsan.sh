#!/usr/bin/env bash
set -euo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${1:-"${project_dir}/build/person4-tsan"}
frontier=${3:-mutex}
case "${frontier}" in
    mutex)
        targets=(verify_correctness test_atomic_state test_gadget_transform)
        tests='^(verify_correctness|test_atomic_state|test_gadget_transform)$'
        ;;
    concurrent)
        targets=(verify_correctness test_d_stress)
        tests='^(verify_correctness_concurrent|test_d_stress)$'
        ;;
    *)
        printf 'Usage: %s [build_dir] [log_path] [mutex|concurrent]\n' "$0" >&2
        exit 2
        ;;
esac
log_path=${2:-"${project_dir}/person4_results/tsan_${frontier}.txt"}

mkdir -p -- "$(dirname -- "${log_path}")"
export OMP_NUM_THREADS=${OMP_NUM_THREADS:-4}
export TSAN_OPTIONS=${TSAN_OPTIONS:-halt_on_error=1:exitcode=66}

run_tests() {
    printf 'Frontier: %s\nCompiler request: %s\nTSAN_OPTIONS: %s\nOMP_TOOL_LIBRARIES: %s\n' \
        "${frontier}" "${CXX:-CMake default}" "${TSAN_OPTIONS}" "${OMP_TOOL_LIBRARIES:-OpenMP default}"
    cmake -S "${project_dir}" -B "${build_dir}" -DCMAKE_BUILD_TYPE=Debug \
        -DBUILD_TESTING=ON -DBMSSP_ENABLE_TSAN=ON -DBMSSP_ENABLE_PROFILING=ON &&
    cmake --build "${build_dir}" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}" \
        --target "${targets[@]}" &&
    ctest --test-dir "${build_dir}" --no-tests=error --verbose -R "${tests}"
}

run_tests 2>&1 | tee "${log_path}"
