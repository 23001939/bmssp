import os
import subprocess
import json
import time

datasets = [
    {
        "id": "NY",
        "name": "New York (NY)",
        "gr_path": "bmssp_comparison/repos/cpp/tests/graphs/USA-road-t.NY.gr",
        "txt_path": "bmssp_comparison/data/USA-road-t.NY.txt",
        "n": 264346,
        "m": 733846
    },
    {
        "id": "BAY",
        "name": "San Francisco Bay Area (BAY)",
        "gr_path": "bmssp_comparison/data/USA-road-t.BAY.gr",
        "txt_path": "bmssp_comparison/data/USA-road-t.BAY.txt",
        "n": 321270,
        "m": 800172
    },
    {
        "id": "COL",
        "name": "Colorado (COL)",
        "gr_path": "bmssp_comparison/data/USA-road-t.COL.gr",
        "txt_path": "bmssp_comparison/data/USA-road-t.COL.txt",
        "n": 435666,
        "m": 1057066
    }
]

bench_rust = "bmssp_comparison/repos/rust_python/target/release/examples/bench_rust.exe"
bench_v2 = "bmssp_comparison/benchmark_v2.exe"
batch_runner = "bmssp_visualizer_mizchi/native_rust/target/release/batch_runner.exe"

results = []

for ds in datasets:
    d_id = ds["id"]
    name = ds["name"]
    txt_path = ds["txt_path"]
    gr_size = os.path.getsize(ds["gr_path"])
    txt_size = os.path.getsize(txt_path)
    n = ds["n"]
    m = ds["m"]
    
    print(f"\n==========================================")
    print(f"BENCHMARKING: {name} (V={n}, E={m})")
    print(f"==========================================")
    
    row = {
        "id": d_id,
        "name": name,
        "n": n,
        "m": m,
        "gr_bytes": gr_size,
        "txt_bytes": txt_size,
        "solvers": {}
    }
    
    # 1. Rust Dijkstra (logtwothirds)
    out_dist = f"bmssp_comparison/data/{d_id}_rust_dijkstra.txt"
    p = subprocess.run([bench_rust, "dijkstra", txt_path, out_dist], capture_output=True, text=True)
    if p.returncode == 0:
        data = json.loads(p.stdout)
        row["solvers"]["rust_dijkstra"] = {"solve_ms": data["median_ms"], "samples": data["samples_ms"]}
        print(f"  [Rust Dijkstra]        solve_ms: {data['median_ms']:.2f}")
    else:
        print(f"  [Rust Dijkstra] FAILED: {p.stderr}")
        
    # 2. Rust BMSSP-fast (logtwothirds)
    out_dist_fast = f"bmssp_comparison/data/{d_id}_rust_bmssp_fast.txt"
    p = subprocess.run([bench_rust, "bmssp-fast", txt_path, out_dist_fast], capture_output=True, text=True)
    if p.returncode == 0:
        data = json.loads(p.stdout)
        row["solvers"]["rust_bmssp_fast"] = {"solve_ms": data["median_ms"], "samples": data["samples_ms"]}
        print(f"  [Rust BMSSP-fast]      solve_ms: {data['median_ms']:.2f}")
    else:
        print(f"  [Rust BMSSP-fast] FAILED: {p.stderr}")
        
    # 3. C++ Dijkstra (benchmark_v2)
    p = subprocess.run([bench_v2, "dijkstra", txt_path, "timing"], capture_output=True, text=True)
    if p.returncode == 0:
        data = json.loads(p.stdout)
        row["solvers"]["cpp_dijkstra"] = {
            "setup_ms": data["setup_ms"],
            "solve_ms": data["solve_ms"],
            "total_ms": data["parsed_to_result_ms"]
        }
        print(f"  [C++ Dijkstra]         solve_ms: {data['solve_ms']:.2f} (total: {data['parsed_to_result_ms']:.2f})")
    else:
        print(f"  [C++ Dijkstra] FAILED: {p.stderr}")
        
    # 4. C++ BMSSP (no transform)
    p = subprocess.run([bench_v2, "bmssp", txt_path, "timing"], capture_output=True, text=True)
    if p.returncode == 0:
        data = json.loads(p.stdout)
        row["solvers"]["cpp_bmssp_notransform"] = {
            "setup_ms": data["setup_ms"],
            "solve_ms": data["solve_ms"],
            "total_ms": data["parsed_to_result_ms"]
        }
        print(f"  [C++ BMSSP No-Trans]   solve_ms: {data['solve_ms']:.2f} (total: {data['parsed_to_result_ms']:.2f})")
    else:
        print(f"  [C++ BMSSP No-Trans] FAILED: {p.stderr}")
        
    # 5. C++ BMSSP (with transform)
    p = subprocess.run([bench_v2, "bmssp_transform", txt_path, "timing"], capture_output=True, text=True)
    if p.returncode == 0:
        data = json.loads(p.stdout)
        row["solvers"]["cpp_bmssp_transform"] = {
            "setup_ms": data["setup_ms"],
            "solve_ms": data["solve_ms"],
            "total_ms": data["parsed_to_result_ms"],
            "n_expanded": data["n_actual"],
            "m_expanded": data["m_actual"]
        }
        print(f"  [C++ BMSSP Transform]  solve_ms: {data['solve_ms']:.2f} (N'={data['n_actual']}, total: {data['parsed_to_result_ms']:.2f})")
    else:
        print(f"  [C++ BMSSP Transform] FAILED: {p.stderr}")
        
    # 6. Rust C-HD (mizchi)
    p = subprocess.run([batch_runner, txt_path, "0", "chd"], capture_output=True, text=True)
    if p.returncode == 0:
        data = json.loads(p.stdout)
        row["solvers"]["rust_chd"] = {
            "compute_ms": data["compute_ms"],
            "preprocess_ms": data["stats"].get("preprocess_ms"),
            "core_ms": data["stats"].get("core_ms")
        }
        print(f"  [Rust C-HD (mizchi)]   compute_ms: {data['compute_ms']:.2f} (core: {data['stats'].get('core_ms', 0):.2f})")
    else:
        print(f"  [Rust C-HD (mizchi)] FAILED: {p.stderr}")
        
    # 7. Rust DMM+25 (mizchi)
    p = subprocess.run([batch_runner, txt_path, "0", "dmm25"], capture_output=True, text=True)
    if p.returncode == 0:
        data = json.loads(p.stdout)
        row["solvers"]["rust_dmm25"] = {
            "compute_ms": data["compute_ms"],
            "preprocess_ms": data["stats"].get("preprocess_ms"),
            "core_ms": data["stats"].get("core_ms"),
            "relax_calls": data["stats"].get("relax_calls")
        }
        print(f"  [Rust DMM25 (mizchi)]  compute_ms: {data['compute_ms']:.2f} (core: {data['stats'].get('core_ms', 0):.2f})")
    else:
        print(f"  [Rust DMM25 (mizchi)] FAILED: {p.stderr}")
        
    # 8. Check distance match between Rust Dijkstra and Rust BMSSP-fast
    with open(out_dist) as f1, open(out_dist_fast) as f2:
        d1 = [float(x.strip()) for x in f1 if x.strip()]
        d2 = [float(x.strip()) for x in f2 if x.strip()]
        diff = max(abs(a - b) for a, b in zip(d1, d2))
        row["dist_match_dijkstra_vs_bmssp_fast"] = (diff == 0.0)
        print(f"  [Accuracy Check] Max distance diff: {diff} (PASS: {diff == 0.0})")

    results.append(row)

out_json = "bmssp_comparison/road_benchmark_3_results.json"
with open(out_json, "w", encoding="utf-8") as f:
    json.dump(results, f, indent=2)
print(f"\nAll benchmark results saved to {out_json}")
