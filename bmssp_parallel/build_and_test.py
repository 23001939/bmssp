#!/usr/bin/env python3
"""Build and execution harness for Parallel BMSSP deliverables."""
import argparse
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def run_cmd(cmd, desc):
    print(f"[*] {desc}: {' '.join(cmd)}")
    start = time.perf_counter()
    res = subprocess.run(
        cmd,
        cwd=str(ROOT),
        capture_output=True,
        encoding="utf-8",
        errors="replace",
    )
    elapsed = time.perf_counter() - start
    if res.returncode != 0:
        print(f"[FAIL] {desc} (Exit code {res.returncode}, {elapsed:.2f}s)")
        if res.stdout:
            print("--- STDOUT ---")
            print(res.stdout)
        if res.stderr:
            print("--- STDERR ---")
            print(res.stderr)
        sys.exit(1)
    print(f"[OK] {desc} ({elapsed:.2f}s)")
    if res.stderr:
        print(res.stderr, end="")
    return res


def main():
    parser = argparse.ArgumentParser(
        description="Build and test the Parallel BMSSP C++20/OpenMP system."
    )
    parser.add_argument(
        "--bench",
        action="store_true",
        help="Run benchmarks/bench_parallel_bmssp.exe after all test suites pass.",
    )
    args = parser.parse_args()

    print("=" * 70)
    print("  BUILDING AND VERIFYING PARALLEL BMSSP SYSTEM")
    print("=" * 70)

    cxx_flags = [
        "g++",
        "-std=c++20",
        "-O3",
        "-march=native",
        "-mcx16",
        "-Wall",
        "-Wextra",
        "-fopenmp",
    ]
    includes = [
        "-Iinclude",
        "-Iinclude/baseline/external",
        "-Iinclude/baseline/upstream/include",
    ]

    core_sources = [
        ("src/core/graph.cpp", "src/core/graph.o"),
        ("src/baseline/bmssp_seq.cpp", "src/baseline/bmssp_seq.o"),
    ]
    ds_sources = [
        ("src/data_structure/d0_deque.cpp", "src/data_structure/d0_deque.o"),
        ("src/data_structure/d1_skiplist.cpp", "src/data_structure/d1_skiplist.o"),
        (
            "src/data_structure/concurrent_frontier_d.cpp",
            "src/data_structure/concurrent_frontier_d.o",
        ),
    ]
    algo_sources = [
        ("src/algorithm/find_pivots.cpp", "src/algorithm/find_pivots.o"),
        ("src/algorithm/base_case.cpp", "src/algorithm/base_case.o"),
        ("src/algorithm/bmssp.cpp", "src/algorithm/bmssp.o"),
    ]

    # 1. Compile all core, data-structure, and algorithm object files
    core_objs = []
    ds_objs = []
    algo_objs = []
    for group_sources, group_objs in [
        (core_sources, core_objs),
        (ds_sources, ds_objs),
        (algo_sources, algo_objs),
    ]:
        for src, obj in group_sources:
            cmd = cxx_flags + includes + ["-c", src, "-o", obj]
            run_cmd(cmd, f"Compiling {src}")
            group_objs.append(obj)

    all_objs = core_objs + ds_objs + algo_objs

    # 2. Compile and link all 4 test executables and benchmark executable
    executables = [
        (
            "tests/test_person1_comprehensive.cpp",
            core_objs,
            "tests/test_person1.exe",
            "Person 1 infrastructure test suite",
        ),
        (
            "tests/test_d_stress.cpp",
            core_objs + ds_objs,
            "tests/test_d_stress.exe",
            "Person 2 ConcurrentFrontierD stress suite",
        ),
        (
            "tests/verify_parallel_bmssp.cpp",
            all_objs,
            "tests/verify_parallel_bmssp.exe",
            "Parallel BMSSP verification suite",
        ),
        (
            "tests/test_parallel_edge_cases.cpp",
            all_objs,
            "tests/test_parallel_edge_cases.exe",
            "Parallel BMSSP edge cases suite",
        ),
        (
            "benchmarks/bench_parallel_bmssp.cpp",
            all_objs,
            "benchmarks/bench_parallel_bmssp.exe",
            "Parallel BMSSP benchmark executable",
        ),
    ]

    for src, objs, exe, label in executables:
        link_cmd = cxx_flags + includes + [src] + objs + ["-o", exe]
        run_cmd(link_cmd, f"Building {label}")

    # 3. Execute all 4 test suites sequentially
    test_runs = [
        ("tests/test_person1.exe", "PERSON 1 INFRASTRUCTURE TEST SUITE"),
        ("tests/test_d_stress.exe", "PERSON 2 CONCURRENT FRONTIER D STRESS SUITE"),
        ("tests/verify_parallel_bmssp.exe", "PARALLEL BMSSP VERIFICATION TEST SUITE"),
        ("tests/test_parallel_edge_cases.exe", "PARALLEL BMSSP EDGE CASES TEST SUITE"),
    ]

    for exe, banner in test_runs:
        print("-" * 70)
        print(f"  EXECUTING {banner}")
        print("-" * 70)
        res = run_cmd([str(ROOT / exe)], f"Running {exe}")
        if res.stdout:
            print(res.stdout, end="" if res.stdout.endswith("\n") else "\n")

    # 4. Optionally run benchmark when --bench is passed
    if args.bench:
        print("-" * 70)
        print("  EXECUTING PARALLEL BMSSP BENCHMARK SUITE")
        print("-" * 70)
        res_bench = run_cmd(
            [str(ROOT / "benchmarks/bench_parallel_bmssp.exe")],
            "Running benchmarks/bench_parallel_bmssp.exe",
        )
        if res_bench.stdout:
            print(res_bench.stdout, end="" if res_bench.stdout.endswith("\n") else "\n")


if __name__ == "__main__":
    main()
