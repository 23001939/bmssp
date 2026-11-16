"""Comprehensive empirical complexity scaling benchmark: BMSSP vs Dijkstra across multiple scales."""
from __future__ import annotations

import json
import math
import subprocess
import time
from pathlib import Path
from typing import Any, Dict, List

ROOT = Path(__file__).resolve().parent
BENCH_CPP = ROOT / 'bmssp_comparison' / 'bench_cpp.exe'
BATCH_RUST = ROOT / 'bmssp_visualizer_mizchi' / 'native_rust' / 'target' / 'release' / 'batch_runner.exe'

DATASETS = [
    ('demo_18', ROOT / 'bmssp_visualizer_mizchi' / 'data' / 'demo.txt', 18, 30),
    ('weighted_50', ROOT / 'bmssp_visualizer_mizchi' / 'data' / 'weighted_50.txt', 50, 120),
    ('weighted_100', ROOT / 'bmssp_visualizer_mizchi' / 'data' / 'weighted_100.txt', 100, 240),
    ('real_2000', ROOT / 'bmssp_comparison' / 'data' / 'real_2000.txt', 2000, 7996),
    ('real_10000', ROOT / 'bmssp_comparison' / 'data' / 'real_10000.txt', 10000, 39996),
    ('real_50000', ROOT / 'bmssp_comparison' / 'data' / 'real_50000.txt', 50000, 199996),
]


def run_cpp(mode: str, input_path: Path) -> float:
    out_txt = ROOT / 'temp_out.txt'
    # bench_cpp.exe mode input output
    res = subprocess.run([str(BENCH_CPP), mode.lower(), str(input_path), str(out_txt)], capture_output=True, text=True, check=True)
    out_txt.unlink(missing_ok=True)
    data = json.loads(res.stdout)
    kernels = sorted(data['kernel_ms'])
    mid = len(kernels) // 2
    return float(kernels[mid]) if len(kernels) % 2 == 1 else float((kernels[mid - 1] + kernels[mid]) / 2.0)



def run_rust(algo: str, input_path: Path) -> float:
    # batch_runner.exe INPUT SOURCE ALGORITHM
    res = subprocess.run([str(BATCH_RUST), str(input_path), '0', algo], capture_output=True, text=True, check=True)
    data = json.loads(res.stdout.strip())
    return float(data['compute_ms'])


def main():
    print("=" * 80)
    print("KHẢO SÁT THỰC NGHIỆM ĐỘ PHỨC TẠP: BMSSP vs DIJKSTRA QUA CÁC THANG ĐO ĐỒ THỊ")
    print("=" * 80)

    results = []

    for name, path, n, m in DATASETS:
        if not path.exists():
            print(f"Bỏ qua {name} (không tìm thấy {path})")
            continue

        print(f"\nĐang đo đồ thị: {name:<14} (n = {n:<6}, m = {m:<7})")

        # C++ solvers (warmup 1 + measure 5)
        t_cpp_dijkstra = run_cpp('dijkstra', path)
        t_cpp_bmssp_wc = run_cpp('wc_bounded', path)
        t_cpp_bmssp_exp = run_cpp('expected_bounded', path)

        # Rust solvers (run 3 times take min/median)
        t_rust_dijkstra = min(run_rust('dijkstra', path) for _ in range(3))
        t_rust_dmm25 = min(run_rust('dmm25', path) for _ in range(3))
        t_rust_chd = min(run_rust('chd', path) for _ in range(3))

        ratio_cpp_wc = t_cpp_bmssp_wc / max(t_cpp_dijkstra, 1e-6)
        ratio_rust_dmm = t_rust_dmm25 / max(t_rust_dijkstra, 1e-6)
        ratio_rust_chd = t_rust_chd / max(t_rust_dijkstra, 1e-6)

        theoretical_ratio = (math.log2(n) ** (2/3)) / math.log2(n) if n > 1 else 1.0

        rec = {
            'name': name,
            'n': n,
            'm': m,
            'theoretical_ratio_log': theoretical_ratio,
            'cpp_dijkstra_ms': t_cpp_dijkstra,
            'cpp_bmssp_wc_ms': t_cpp_bmssp_wc,
            'cpp_bmssp_exp_ms': t_cpp_bmssp_exp,
            'ratio_cpp_wc_vs_dijkstra': ratio_cpp_wc,
            'rust_dijkstra_ms': t_rust_dijkstra,
            'rust_bmssp_dmm25_ms': t_rust_dmm25,
            'rust_bmssp_chd_ms': t_rust_chd,
            'ratio_rust_dmm_vs_dijkstra': ratio_rust_dmm,
            'ratio_rust_chd_vs_dijkstra': ratio_rust_chd,
        }
        results.append(rec)

        print(f"  [C++]  Dijkstra: {t_cpp_dijkstra:8.3f} ms | BMSSP-WC: {t_cpp_bmssp_wc:8.3f} ms | BMSSP-Exp: {t_cpp_bmssp_exp:8.3f} ms | Tỷ số WC/Dijkstra: {ratio_cpp_wc:6.2f}x")
        print(f"  [Rust] Dijkstra: {t_rust_dijkstra:8.3f} ms | DMM+25:   {t_rust_dmm25:8.3f} ms | C-HD:      {t_rust_chd:8.3f} ms | Tỷ số DMM/Dijkstra: {ratio_rust_dmm:6.2f}x | C-HD/Dijkstra: {ratio_rust_chd:5.2f}x")
        print(f"  [Lý thuyết] log^(2/3)(n) / log(n) = {theoretical_ratio:.4f} (lý thuyết dự đoán BMSSP phải nhanh hơn Dijkstra ~{1/theoretical_ratio:.2f}x)")

    # Lưu kết quả
    (ROOT / 'complexity_comparison_results.json').write_text(json.dumps(results, indent=2, ensure_ascii=False), encoding='utf-8')
    print("\n[+] Đã lưu toàn bộ số liệu vào complexity_comparison_results.json")


if __name__ == '__main__':
    main()
