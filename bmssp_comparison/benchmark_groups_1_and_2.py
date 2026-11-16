import os
import sys
import json
import time
import subprocess
import math

DATA_DIR = r"C:\ttss\bmssp_comparison\data"
RESULTS_FILE = r"C:\ttss\bmssp_comparison\road_benchmark_groups_1_2_results.json"

DATASETS = [
    {
        "id": "FLA",
        "name": "Florida (FLA) - Nhóm 1",
        "path": os.path.join(DATA_DIR, "USA-road-d.FLA.txt"),
        "expected_n": 1070376,
        "expected_m": 2712798,
        "group": "Nhóm 1 (1M - 2M đỉnh)",
    },
    {
        "id": "TX",
        "name": "Texas (TX) - Nhóm 2",
        "path": os.path.join(DATA_DIR, "USA-road-d.TX.txt"),
        "expected_n": 2073870,
        "expected_m": 5168318,
        "group": "Nhóm 2 (2M - 6M đỉnh)",
    }
]

BENCH_RUST = r"C:\ttss\bmssp_comparison\repos\rust_python\target\release\examples\bench_rust.exe"
BENCH_CPP = r"C:\ttss\bmssp_comparison\benchmark_v2.exe"
BENCH_MIZCHI = r"C:\ttss\bmssp_visualizer_mizchi\native_rust\target\release\batch_runner.exe"

def run_bench():
    all_results = []
    
    for ds in DATASETS:
        ds_id = ds["id"]
        file_path = ds["path"]
        print(f"\n==========================================", flush=True)
        print(f"BENCHMARKING: {ds['name']}", flush=True)
        print(f"File: {file_path}", flush=True)
        print(f"File size: {os.path.getsize(file_path) / 1024 / 1024:.2f} MB", flush=True)
        print(f"==========================================", flush=True)

        res_entry = {
            "id": ds["id"],
            "name": ds["name"],
            "group": ds["group"],
            "file_size_mb": round(os.path.getsize(file_path) / 1024 / 1024, 2),
            "n": ds["expected_n"],
            "m": ds["expected_m"],
            "solvers": {}
        }

        # 1. Rust Dijkstra (4-ary heap SoA)
        print("-> Running Rust Dijkstra (4-ary heap SoA)...", flush=True)
        dist_dijkstra = os.path.join(DATA_DIR, f"dist_rust_dijkstra_{ds_id.lower()}.txt")
        out = subprocess.check_output([BENCH_RUST, "dijkstra", file_path, dist_dijkstra], text=True)
        data_dijk = json.loads(out)
        res_entry["solvers"]["rust_dijkstra_soa"] = {
            "solve_ms": data_dijk["median_ms"],
            "samples_ms": data_dijk["samples_ms"]
        }
        print(f"   Median: {data_dijk['median_ms']:.2f} ms", flush=True)

        # 2. Rust BMSSP-fast
        print("-> Running Rust BMSSP-fast...", flush=True)
        dist_bmssp = os.path.join(DATA_DIR, f"dist_rust_bmssp_{ds_id.lower()}.txt")
        out = subprocess.check_output([BENCH_RUST, "bmssp-fast", file_path, dist_bmssp], text=True)
        data_fast = json.loads(out)
        res_entry["solvers"]["rust_bmssp_fast"] = {
            "solve_ms": data_fast["median_ms"],
            "samples_ms": data_fast["samples_ms"],
            "ratio_to_dijkstra": round(data_fast["median_ms"] / data_dijk["median_ms"], 2)
        }
        print(f"   Median: {data_fast['median_ms']:.2f} ms ({res_entry['solvers']['rust_bmssp_fast']['ratio_to_dijkstra']}x Dijkstra)", flush=True)

        # Verify distance correctness
        print("-> Verifying distance accuracy between Rust Dijkstra and Rust BMSSP-fast...", flush=True)
        with open(dist_dijkstra) as f1, open(dist_bmssp) as f2:
            d1 = [float(x.strip()) for x in f1]
            d2 = [float(x.strip()) for x in f2]
        diffs = [abs(a - b) for a, b in zip(d1, d2) if math.isfinite(a) and math.isfinite(b)]
        max_diff = max(diffs) if diffs else 0.0
        unreach1 = sum(1 for a in d1 if not math.isfinite(a))
        unreach2 = sum(1 for b in d2 if not math.isfinite(b))
        res_entry["correctness"] = {
            "max_distance_diff": max_diff,
            "match_100_percent": (max_diff == 0.0 and unreach1 == unreach2),
            "unreachable_count": unreach1
        }
        print(f"   Max Diff: {max_diff:.1f}, Unreachable: {unreach1}, Match 100%: {res_entry['correctness']['match_100_percent']}", flush=True)

        # 3. C++ Dijkstra
        print("-> Running C++ Dijkstra (std::priority_queue)...", flush=True)
        out = subprocess.check_output([BENCH_CPP, "dijkstra", file_path, "timing"], text=True)
        data_cpp_dijk = json.loads(out)
        res_entry["solvers"]["cpp_dijkstra"] = {
            "setup_ms": round(data_cpp_dijk["setup_ms"], 2),
            "solve_ms": round(data_cpp_dijk["solve_ms"], 2),
            "total_ms": round(data_cpp_dijk["parsed_to_result_ms"], 2)
        }
        print(f"   Solve: {data_cpp_dijk['solve_ms']:.2f} ms, Total: {data_cpp_dijk['parsed_to_result_ms']:.2f} ms", flush=True)

        # 4. C++ BMSSP (no transform)
        print("-> Running C++ BMSSP (direct degree)...", flush=True)
        out = subprocess.check_output([BENCH_CPP, "bmssp", file_path, "timing"], text=True)
        data_cpp_bmssp = json.loads(out)
        res_entry["solvers"]["cpp_bmssp_notransform"] = {
            "setup_ms": round(data_cpp_bmssp["setup_ms"], 2),
            "solve_ms": round(data_cpp_bmssp["solve_ms"], 2),
            "total_ms": round(data_cpp_bmssp["parsed_to_result_ms"], 2),
            "ratio_to_cpp_dijkstra": round(data_cpp_bmssp["solve_ms"] / data_cpp_dijk["solve_ms"], 2)
        }
        print(f"   Solve: {data_cpp_bmssp['solve_ms']:.2f} ms ({res_entry['solvers']['cpp_bmssp_notransform']['ratio_to_cpp_dijkstra']}x C++ Dijkstra)", flush=True)

        # 5. C++ BMSSP (with Gadget Transform d<=2)
        print("-> Running C++ BMSSP (Gadget Transform d<=2)...", flush=True)
        out = subprocess.check_output([BENCH_CPP, "bmssp_transform", file_path, "timing"], text=True)
        data_cpp_trans = json.loads(out)
        res_entry["solvers"]["cpp_bmssp_transform"] = {
            "setup_ms": round(data_cpp_trans["setup_ms"], 2),
            "solve_ms": round(data_cpp_trans["solve_ms"], 2),
            "total_ms": round(data_cpp_trans["parsed_to_result_ms"], 2),
            "n_expanded": data_cpp_trans["n_actual"],
            "m_expanded": data_cpp_trans["m_actual"],
            "ratio_to_cpp_dijkstra": round(data_cpp_trans["solve_ms"] / data_cpp_dijk["solve_ms"], 2)
        }
        print(f"   Expanded: n={data_cpp_trans['n_actual']}, m={data_cpp_trans['m_actual']}. Solve: {data_cpp_trans['solve_ms']:.2f} ms ({res_entry['solvers']['cpp_bmssp_transform']['ratio_to_cpp_dijkstra']}x C++ Dijkstra)", flush=True)

        # 6. Mizchi Rust C-HD
        print("-> Running Mizchi Rust C-HD...", flush=True)
        out = subprocess.check_output([BENCH_MIZCHI, file_path, "0", "chd"], text=True)
        data_chd = json.loads(out)
        stats = data_chd.get("stats", {})
        res_entry["solvers"]["rust_mizchi_chd"] = {
            "compute_ms": round(data_chd["compute_ms"], 2),
            "preprocess_ms": round(stats.get("preprocess_ms", 0.0), 2),
            "core_ms": round(stats.get("core_ms", 0.0), 2),
            "ratio_to_rust_dijkstra": round(data_chd["compute_ms"] / data_dijk["median_ms"], 2)
        }
        print(f"   Compute: {data_chd['compute_ms']:.2f} ms (Preprocess: {stats.get('preprocess_ms', 0):.2f} ms, Core: {stats.get('core_ms', 0):.2f} ms)", flush=True)

        all_results.append(res_entry)

    with open(RESULTS_FILE, "w", encoding="utf-8") as f:
        json.dump(all_results, f, indent=2, ensure_ascii=False)
    print(f"\nAll benchmark results successfully saved to {RESULTS_FILE}!", flush=True)

if __name__ == "__main__":
    run_bench()
