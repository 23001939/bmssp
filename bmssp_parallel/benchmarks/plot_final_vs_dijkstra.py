#!/usr/bin/env python3
"""Generate comparison chart from bmssp_parallel/final_benchmark_1m_2m.csv."""
import csv
import sys
from pathlib import Path


def main():
    csv_path = Path(
        sys.argv[1] if len(sys.argv) > 1 else "bmssp_parallel/final_benchmark_1m_2m.csv"
    )
    out_png = Path(
        sys.argv[2]
        if len(sys.argv) > 2
        else "bmssp_parallel/final_vs_dijkstra_1m_2m.png"
    )

    with csv_path.open("r", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))

    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(15, 6), dpi=160)

    # Panel 1: 1M & 2M Thread Scaling vs Dijkstra Baseline
    target_ids = ["ER_1M", "SF_1M", "D12_1M", "ER_2M", "SF_2M", "D12_2M"]
    threads = [1, 2, 4, 8, 12]
    colors = {
        "ER_1M": "#2563eb",
        "SF_1M": "#dc2626",
        "D12_1M": "#059669",
        "ER_2M": "#1d4ed8",
        "SF_2M": "#b91c1c",
        "D12_2M": "#047857",
    }
    styles = {
        "ER_1M": "--o",
        "SF_1M": "--s",
        "D12_1M": "--^",
        "ER_2M": "-o",
        "SF_2M": "-s",
        "D12_2M": "-^",
    }

    for r in rows:
        gid = r["id"]
        if gid not in target_ids:
            continue
        dijk = float(r["dijkstra_1t_ms"])
        ys = [
            dijk / float(r[f"bmssp_{t}t_ms"])
            for t in threads
        ]
        ax1.plot(
            threads,
            ys,
            styles[gid],
            color=colors[gid],
            linewidth=2.0,
            markersize=6,
            label=f'{r["label"]} (vs Dijk={dijk:.0f}ms)',
        )

    ax1.axhline(1.0, color="#111827", linestyle=":", linewidth=1.6, label="Dijkstra (1T) Baseline = 1.0x")
    ax1.set_xticks(threads)
    ax1.set_xlabel("Số luồng OpenMP (Threads)", fontsize=11)
    ax1.set_ylabel("Hệ số nhanh hơn so với Dijkstra (Dijkstra_ms / BMSSP_ms)", fontsize=11)
    ax1.set_title("Tăng tốc Parallel BMSSP so với Dijkstra (1M – 2M đỉnh)", fontsize=12, fontweight="bold")
    ax1.grid(True, alpha=0.3)
    ax1.legend(fontsize=8.5, loc="upper left")

    # Panel 2: Direct Median Execution Time Comparison on 1M & 2M Graphs
    focus_rows = [r for r in rows if r["id"] in target_ids]
    labels = [r["id"].replace("_", "\n") for r in focus_rows]
    x = np.arange(len(focus_rows))
    width = 0.25

    dijk_vals = [float(r["dijkstra_1t_ms"]) for r in focus_rows]
    b1_vals = [float(r["bmssp_1t_ms"]) for r in focus_rows]
    b_best_vals = [float(r["best_par_ms"]) for r in focus_rows]

    rects1 = ax2.bar(x - width, dijk_vals, width, label="Dijkstra (1T)", color="#64748b")
    rects2 = ax2.bar(x, b1_vals, width, label="BMSSP (1T)", color="#f59e0b")
    rects3 = ax2.bar(x + width, b_best_vals, width, label="Parallel BMSSP (Tối ưu 8T/12T)", color="#10b981")

    for rect in rects1:
        h = rect.get_height()
        ax2.annotate(f"{h:.0f}", (rect.get_x() + rect.get_width() / 2, h),
                     xytext=(0, 3), textcoords="offset points", ha="center", fontsize=7.5)
    for rect in rects3:
        h = rect.get_height()
        ax2.annotate(f"{h:.0f}", (rect.get_x() + rect.get_width() / 2, h),
                     xytext=(0, 3), textcoords="offset points", ha="center", fontsize=7.5, fontweight="bold")

    ax2.set_xticks(x)
    ax2.set_xticklabels(labels, fontsize=9.5)
    ax2.set_ylabel("Thời gian chạy trung vị (ms — càng thấp càng nhanh)", fontsize=11)
    ax2.set_title("So sánh thời gian chạy thực tế (1M – 2M đỉnh)", fontsize=12, fontweight="bold")
    ax2.grid(True, axis="y", alpha=0.3)
    ax2.legend(fontsize=9)

    fig.tight_layout()
    out_png.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_png)
    print(f"[OK] Saved chart to {out_png}")


if __name__ == "__main__":
    main()
