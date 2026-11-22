#!/usr/bin/env python3
"""Run and verify synthetic Concurrent BMSSP benchmark configurations sequentially."""

import argparse
import csv
import hashlib
import io
import itertools
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys


REPO = Path(__file__).resolve().parents[2]
TIMES = ("generation_ms", "oracle_ms", "transform_ms", "solve_ms", "restore_ms", "total_ms")
GRAPH_FIELDS = ("n", "m", "seed", "source", "graph_hash", "reachable")


def run_text(args):
    return subprocess.check_output(args, cwd=REPO, text=True, encoding="utf-8").strip()


def metadata(binary, host_cores):
    cache = (binary.parent / "CMakeCache.txt").read_text(encoding="utf-8").splitlines()
    build = {}
    for line in cache:
        if not line.startswith(("#", "//")) and ":" in line and "=" in line:
            variable, value = line.split("=", 1)
            build[variable.split(":", 1)[0]] = value
    if build.get("CMAKE_BUILD_TYPE") != "Release" or build.get("BMSSP_ENABLE_TSAN") == "ON":
        raise ValueError("Configure a Release build without ThreadSanitizer")
    compile_db = json.loads((binary.parent / "compile_commands.json").read_text(encoding="utf-8"))
    commands = {Path(row["file"]).name: row.get("command", row.get("arguments")) for row in compile_db
                if Path(row["file"]).name in {"bench_synthetic.cpp", "bmssp.cpp", "find_pivots.cpp"}}
    source_hash = hashlib.sha256()
    paths = run_text(["git", "ls-files", "-co", "--exclude-standard", "--", "bmssp_parallel"]).splitlines()
    for name in sorted(set(paths)):
        path = REPO / name
        if path.is_file() and (path.suffix in {".cpp", ".hpp", ".h", ".py"} or path.name == "CMakeLists.txt"):
            source_hash.update(name.encode() + b"\0" + path.read_bytes() + b"\0")
    cpu = {}
    if sys.platform.startswith("linux"):
        output = run_text(["lscpu", "--json"])
        cpu.update({row["field"].rstrip(":"): row["data"] for row in json.loads(output)["lscpu"]})
    else:
        cpu["Model name"] = platform.processor()
        cpu["CPU(s)"] = os.cpu_count()
    if host_cores is None and "Core(s) per socket" in cpu and "Socket(s)" in cpu and "Hypervisor vendor" not in cpu:
        host_cores = int(cpu["Core(s) per socket"]) * int(cpu["Socket(s)"])
    return {
        "commit": run_text(["git", "rev-parse", "HEAD"]),
        "working_tree_has_changes": bool(run_text(["git", "-c", "core.filemode=false", "status", "--porcelain"])),
        "benchmark_source_sha256": source_hash.hexdigest(),
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "compiler_version": run_text([build["CMAKE_CXX_COMPILER"], "--version"]).splitlines()[0],
        "compile_commands": commands, "profiling": build.get("BMSSP_ENABLE_PROFILING") == "ON",
        "platform": platform.platform(), "physical_host_cores": host_cores, "cpu_topology": cpu,
        "openmp_environment": {"OMP_PROC_BIND": "close", "OMP_PLACES": "cores", "OMP_DYNAMIC": "FALSE"},
        "speedup_baseline": "Concurrent BMSSP, one actual OpenMP thread, same graph and solver options",
        "solver_timing": "BMSSP core call including solver setup and result extraction; excludes oracle and graph generation",
        "total_timing": "Wall time of BMSSP API call, including gadget transformation and restoration when enabled",
        "threads_actual_definition": "Team size from an OpenMP probe with the same requested size and affinity",
    }


def measure(args, family, n, threads, cutoff, cap, environment):
    run = {"family": family, "n": n, "threads_requested": threads, "cutoff": cutoff, "block_cap": cap}
    command = [str(args.binary), family, *map(str, (n, args.degree, args.seed, args.source, threads,
                                               cutoff, cap, int(args.gadget), args.warmups, args.samples))]
    print(f"{family} n={n} threads={threads} cutoff={cutoff} cap={cap}", flush=True)
    try:
        result = subprocess.run(command, env=environment, capture_output=True, text=True,
                                encoding="utf-8", timeout=args.timeout, check=False)
    except subprocess.TimeoutExpired:
        return {**run, "status": "timeout", "timeout_sec": args.timeout}
    if result.returncode:
        return {**run, "status": "failed", "exit_code": result.returncode,
                "stderr": result.stderr, "stdout": result.stdout}
    rows = list(csv.DictReader(io.StringIO(result.stdout)))
    if len(rows) != args.samples:
        raise ValueError(f"Expected {args.samples} samples, got {len(rows)} for {run}")
    if any(row[field] != rows[0][field] for row in rows for field in (*GRAPH_FIELDS, "threads_actual")):
        raise ValueError(f"Inconsistent graph or thread metadata for {run}")
    expected = {"family": family, "n": str(n), "seed": str(args.seed), "source": str(args.source),
                "threads_requested": str(threads), "cutoff": str(cutoff), "block_cap": str(cap),
                "gadget": str(int(args.gadget))}
    for row in rows:
        if any(row[field] != value for field, value in expected.items()):
            raise ValueError(f"Benchmark returned a different configuration for {run}")
        values = [float(row[field]) for field in TIMES]
        if not all(math.isfinite(value) and value >= 0 for value in values) or float(row["solve_ms"]) <= 0:
            raise ValueError(f"Invalid sample timing for {run}")
    medians = {field: statistics.median(float(row[field]) for row in rows) for field in TIMES}
    return {**run, "status": "ok", "exit_code": 0, "graph": {field: int(rows[0][field]) for field in GRAPH_FIELDS},
            "threads_actual": int(rows[0]["threads_actual"]), "omp_places": int(rows[0]["omp_places"]),
            "medians": medians, "samples": rows, "stderr": result.stderr}


def variant(run):
    return run["family"], run["n"], run["cutoff"], run["block_cap"]


def save_results(output, results):
    temporary = output.with_suffix(".tmp")
    temporary.write_text(json.dumps(results, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    temporary.replace(output)


def summarize(runs, output):
    successful = [run for run in runs if run["status"] == "ok"]
    baselines = {variant(run): run["medians"]["solve_ms"] for run in successful
                 if run["threads_requested"] == run["threads_actual"] == 1}
    key_fields = ("family", "n", "threads_requested", "threads_actual", "cutoff", "block_cap")
    with output.with_suffix(".csv").open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=[*key_fields, "m", "graph_hash", *TIMES, "speedup", "efficiency"])
        writer.writeheader()
        for run in successful:
            baseline = baselines.get(variant(run))
            if baseline is not None:
                run["speedup"] = baseline / run["medians"]["solve_ms"]
                run["efficiency"] = run["speedup"] / run["threads_actual"]
            writer.writerow({**{field: run[field] for field in key_fields}, "m": run["graph"]["m"],
                             "graph_hash": run["graph"]["graph_hash"], **run["medians"],
                             "speedup": run.get("speedup"), "efficiency": run.get("efficiency")})


def plot_results(source, destination):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    runs = json.loads(source.read_text(encoding="utf-8"))["runs"]
    groups = {}
    for run in runs:
        if "speedup" in run:
            groups.setdefault(variant(run), []).append(run)
    if not groups:
        raise ValueError("The result contains no scaling series with a one-thread baseline")
    fig, (speedup, efficiency) = plt.subplots(1, 2, figsize=(11, 4), layout="constrained")
    for key, group in groups.items():
        ordered = sorted(group, key=lambda run: run["threads_actual"])
        threads = [run["threads_actual"] for run in ordered]
        family, n, cutoff, cap = key
        label = f"{family}, n={n:,}" if len(groups) <= 2 else f"{family}, n={n:,}, cutoff={cutoff}, M≤{cap or 'auto'}"
        speedup.plot(threads, [run["speedup"] for run in ordered], "o-", label=label)
        efficiency.plot(threads, [100 * run["efficiency"] for run in ordered], "o-", label=label)
    speedup.axhline(3, linestyle="--", color="0.5", label="Target 3×")
    for axis, title in ((speedup, "Speedup T(1) / T(p)"), (efficiency, "Parallel efficiency (%)")):
        axis.set(xlabel="OpenMP team size (probe)", ylabel=title)
        axis.grid(alpha=0.25)
        axis.legend()
    fig.savefig(destination, dpi=180)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--plot-from", type=Path)
    parser.add_argument("--family", nargs="+", choices=("er", "scale-free"), default=["er", "scale-free"])
    parser.add_argument("--vertices", nargs="+", type=int, default=[1_000_000])
    parser.add_argument("--threads", nargs="+", type=int, default=[1, 2, 4, 8, 16, 32])
    parser.add_argument("--cutoff", nargs="+", type=int, default=[2048])
    parser.add_argument("--block-cap", nargs="+", type=int, default=[0])
    parser.add_argument("--degree", type=int, default=4)
    parser.add_argument("--seed", type=int, default=20261122)
    parser.add_argument("--source", type=int, default=0)
    parser.add_argument("--samples", type=int, default=3)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--host-physical-cores", type=int)
    parser.add_argument("--gadget", action="store_true")
    args = parser.parse_args()
    if args.plot_from:
        plot_results(args.plot_from, args.output)
        return 0
    if not args.binary or not args.binary.is_file() or args.output.suffix != ".json":
        parser.error("Provide --binary and a .json --output")
    if args.output.exists():
        parser.error("Choose a new output file to preserve existing measurements")
    args.binary = args.binary.resolve()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    run_config = {"degree": args.degree, "seed": args.seed, "source": args.source, "samples": args.samples,
                  "warmups": args.warmups, "gadget": args.gadget, "timeout_sec": args.timeout}
    results = {"metadata": metadata(args.binary, args.host_physical_cores), "config": run_config, "runs": []}
    environment = dict(os.environ, **results["metadata"]["openmp_environment"])
    graphs = {}
    for family, n, cutoff, cap, threads in itertools.product(args.family, args.vertices, args.cutoff,
                                                              args.block_cap, args.threads):
        run = measure(args, family, n, threads, cutoff, cap, environment)
        if run["status"] == "ok":
            if run["graph"] != graphs.setdefault((family, n), run["graph"]):
                raise ValueError(f"Graph fingerprint or reachability changed across configurations: {run['graph']}")
        else:
            print(f"  {run['status']}: {run.get('stderr', '')}", file=sys.stderr, flush=True)
        results["runs"].append(run)
        save_results(args.output, results)
    summarize(results["runs"], args.output)
    save_results(args.output, results)
    return int(any(run["status"] != "ok" for run in results["runs"]))


if __name__ == "__main__":
    sys.exit(main())
