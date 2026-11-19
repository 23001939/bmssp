#!/usr/bin/env python3
"""Build and execution harness for Parallel BMSSP deliverables."""
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent

def run_cmd(cmd, desc):
    print(f"[*] {desc}: {' '.join(cmd)}")
    start = time.perf_counter()
    res = subprocess.run(cmd, cwd=str(ROOT), capture_output=True, text=True)
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
    return res

def main():
    print("=" * 70)
    print("  BUILDING AND VERIFYING PARALLEL BMSSP SYSTEM")
    print("=" * 70)

    includes = [
        "-Iinclude",
        "-Iinclude/baseline/external",
        "-Iinclude/baseline/upstream/include"
    ]

    # 1. Compile all core and algorithm object files
    obj_files = []
    cpp_files = [
        ("src/core/graph.cpp", "src/core/graph.o"),
        ("src/baseline/bmssp_seq.cpp", "src/baseline/bmssp_seq.o"),
        ("src/algorithm/find_pivots.cpp", "src/algorithm/find_pivots.o"),
        ("src/algorithm/base_case.cpp", "src/algorithm/base_case.o"),
        ("src/algorithm/bmssp.cpp", "src/algorithm/bmssp.o"),
    ]

    for src, obj in cpp_files:
        cmd = [
            "g++", "-std=c++20", "-O3", "-march=native", "-mcx16", "-fopenmp"
        ] + includes + ["-c", src, "-o", obj]
        run_cmd(cmd, f"Compiling {src}")
        obj_files.append(obj)

    # 2. Compile and link Person 1 test suite
    test_p1_exe = "tests/test_person1.exe"
    link_p1_cmd = [
        "g++", "-std=c++20", "-O3", "-march=native", "-mcx16", "-fopenmp"
    ] + includes + ["tests/test_person1_comprehensive.cpp", "src/core/graph.o", "src/baseline/bmssp_seq.o", "-o", test_p1_exe]
    run_cmd(link_p1_cmd, "Linking Person 1 test suite executable")

    # 3. Compile and link Parallel BMSSP verification test suite
    test_par_exe = "tests/verify_parallel_bmssp.exe"
    link_par_cmd = [
        "g++", "-std=c++20", "-O3", "-march=native", "-mcx16", "-fopenmp"
    ] + includes + ["tests/verify_parallel_bmssp.cpp"] + obj_files + ["-o", test_par_exe]
    run_cmd(link_par_cmd, "Linking Parallel BMSSP verification executable")

    # 4. Execute test suites
    print("-" * 70)
    print("  EXECUTING PERSON 1 INFRASTRUCTURE TEST SUITE")
    print("-" * 70)
    res_p1 = run_cmd([str(ROOT / test_p1_exe)], "Running test_person1.exe")
    print(res_p1.stdout)

    print("-" * 70)
    print("  EXECUTING PARALLEL BMSSP VERIFICATION TEST SUITE")
    print("-" * 70)
    res_par = run_cmd([str(ROOT / test_par_exe)], "Running verify_parallel_bmssp.exe")
    print(res_par.stdout)

if __name__ == "__main__":
    main()
