#!/usr/bin/env python3
"""Build and execution harness for Person 1 deliverables."""
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
    print("  BUILDING AND VERIFYING PERSON 1 INFRASTRUCTURE & PRIMITIVES")
    print("=" * 70)

    includes = [
        "-Iinclude",
        "-Iinclude/baseline/external",
        "-Iinclude/baseline/upstream/include"
    ]

    # 1. Compile core library
    obj_files = []
    cpp_files = [
        ("src/core/graph.cpp", "src/core/graph.o"),
        ("src/baseline/bmssp_seq.cpp", "src/baseline/bmssp_seq.o")
    ]

    for src, obj in cpp_files:
        cmd = [
            "g++", "-std=c++20", "-O3", "-march=native", "-mcx16", "-fopenmp"
        ] + includes + ["-c", src, "-o", obj]
        run_cmd(cmd, f"Compiling {src}")
        obj_files.append(obj)

    # 2. Compile and link comprehensive test suite
    test_exe = "tests/test_person1.exe"
    link_cmd = [
        "g++", "-std=c++20", "-O3", "-march=native", "-mcx16", "-fopenmp"
    ] + includes + ["tests/test_person1_comprehensive.cpp"] + obj_files + ["-o", test_exe]
    run_cmd(link_cmd, "Linking Person 1 test suite executable")

    # 3. Execute test suite
    print("-" * 70)
    print("  EXECUTING VERIFICATION TEST SUITE")
    print("-" * 70)
    test_cmd = [str(ROOT / test_exe)]
    res = run_cmd(test_cmd, "Running test_person1.exe")
    print(res.stdout)

if __name__ == "__main__":
    main()
