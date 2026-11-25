# -*- coding: utf-8 -*-
"""
Sinh bộ 6 biểu đồ thực nghiệm chuẩn học thuật tiếng Việt (300 DPI) cho Chương 3 báo cáo LaTeX.
Tất cả số liệu lấy trực tiếp 100% từ 3 tệp CSV thực nghiệm đã kiểm chứng với Dijkstra Oracle:
  - bmssp_parallel/final_benchmark_1m_2m.csv (11 đồ thị tổng hợp 100K - 2M đỉnh)
  - bmssp_parallel/dimacs_road_7_results.csv (7 mạng đường bộ Hoa Kỳ DIMACS-9)
  - bmssp_parallel/snap_highdeg_results.csv  (5 mạng thực tế bậc cao Stanford SNAP)
"""

import csv
import math
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from matplotlib.colors import TwoSlopeNorm

ROOT = Path(r"c:/ttss")
OUT_DIR = ROOT / "bao_cao_overleaf_ch3" / "image"
OUT_DIR.mkdir(parents=True, exist_ok=True)

plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "mathtext.fontset": "dejavusans",
    "font.size": 10,
    "axes.titlesize": 10.8,
    "axes.titleweight": "bold",
    "axes.labelsize": 9.8,
    "xtick.labelsize": 8.8,
    "ytick.labelsize": 8.8,
    "legend.fontsize": 8.3,
    "figure.dpi": 300,
    "savefig.dpi": 300,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "axes.grid": True,
    "grid.alpha": 0.28,
    "grid.linestyle": "-",
    "grid.linewidth": 0.6,
})

C_DIJK = "#6c757d"
C_BMSSP_1T = "#8da9c4"
C_BMSSP = "#1f5aa6"
C_TEAL = "#0d8a82"
C_ORANGE = "#c45a11"
C_RED = "#b02a37"

def read_csv(path: Path):
    with path.open("r", encoding="utf-8") as f:
        return list(csv.DictReader(f))

syn_rows = read_csv(ROOT / "bmssp_parallel" / "final_benchmark_1m_2m.csv")
road_rows = read_csv(ROOT / "bmssp_parallel" / "dimacs_road_7_results.csv")
snap_rows = read_csv(ROOT / "bmssp_parallel" / "snap_highdeg_results.csv")

# ==============================================================================
# 1. ch3_dataset_landscape.png: Không gian quy mô (n, m) & độ lệch bậc cực đại
# ==============================================================================
def plot_fig_dataset_landscape():
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.6, 4.25), gridspec_kw={"width_ratios": [1.10, 1.05]})

    # Ô (a): Không gian (n, m) của toàn bộ 23 bộ dữ liệu
    ax1.axvspan(4.0e5, 1.0e6, color="#fff3cd", alpha=0.45, label="Vùng chuyển tiếp tràn L3 Cache (16 MB)")
    ax1.scatter([float(r["n"]) for r in syn_rows], [float(r["m"]) for r in syn_rows],
                s=58, marker="o", color=C_BMSSP, edgecolors="white", linewidth=0.7, zorder=4,
                label="Tổng hợp kiểm soát (11 đồ thị)")
    ax1.scatter([float(r["n"]) for r in road_rows], [float(r["m"]) for r in road_rows],
                s=62, marker="^", color=C_ORANGE, edgecolors="white", linewidth=0.7, zorder=4,
                label="Đường bộ DIMACS-9 (7 mạng)")
    ax1.scatter([float(r["n"]) for r in snap_rows], [float(r["m"]) for r in snap_rows],
                s=58, marker="s", color=C_TEAL, edgecolors="white", linewidth=0.7, zorder=4,
                label="Mạng thực tế SNAP (5 mạng)")

    annots = [
        (34546, 421534, "cit-HepPh", (7, -3)),
        (77360, 828161, "Slashdot", (7, -4)),
        (685230, 7600595, "web-BerkStan", (-54, 7)),
        (1632803, 30622564, "soc-Pokec (30,6M cung)", (-92, 6)),
        (1791489, 28508141, "wiki-topcats", (-62, -13)),
        (264346, 730100, "NY", (6, -9)),
        (1890815, 4620230, "CAL (1,89M đỉnh)", (-58, 9)),
        (2000000, 24002527, "D12_2M", (-46, -13)),
        (100000, 400713, "ER_100K", (6, -9)),
    ]
    for nx, my, txt, off in annots:
        ax1.annotate(txt, xy=(nx, my), xytext=off, textcoords="offset points", fontsize=7.6, color="#212529")

    ax1.set_xscale("log")
    ax1.set_yscale("log")
    ax1.set_xlim(2.4e4, 3.2e6)
    ax1.set_ylim(2.5e5, 5.2e7)
    ax1.set_xlabel("Số đỉnh $n$ (thang logarit)")
    ax1.set_ylabel("Số cung có hướng $m$ (thang logarit)")
    ax1.set_title("(a) Không gian quy mô của 23 bộ dữ liệu thực nghiệm")
    ax1.legend(loc="upper left", fontsize=7.9, frameon=True, facecolor="white", framealpha=0.92)

    # Ô (b): So sánh Bậc trung bình d và Bậc ra cực đại max_out_deg (thang log)
    by_syn = {r["id"]: r for r in syn_rows}
    by_road = {r["code"]: r for r in road_rows}
    by_snap = {r["dataset"]: r for r in snap_rows}

    rep = [
        ("NY", float(by_road["NY"]["m"])/float(by_road["NY"]["n"]), float(by_road["NY"]["max_out_deg"])),
        ("CAL", float(by_road["CAL"]["m"])/float(by_road["CAL"]["n"]), float(by_road["CAL"]["max_out_deg"])),
        ("ER_2M", float(by_syn["ER_2M"]["m"])/float(by_syn["ER_2M"]["n"]), float(by_syn["ER_2M"]["max_out_deg"])),
        ("D12_2M", float(by_syn["D12_2M"]["m"])/float(by_syn["D12_2M"]["n"]), float(by_syn["D12_2M"]["max_out_deg"])),
        ("BerkStan", float(by_snap["web-BerkStan"]["avg_deg"]), float(by_snap["web-BerkStan"]["max_out_deg"])),
        ("topcats", float(by_snap["wiki-topcats"]["avg_deg"]), float(by_snap["wiki-topcats"]["max_out_deg"])),
        ("Pokec", float(by_snap["soc-Pokec"]["avg_deg"]), float(by_snap["soc-Pokec"]["max_out_deg"])),
        ("SF_2M", float(by_syn["SF_2M"]["m"])/float(by_syn["SF_2M"]["n"]), float(by_syn["SF_2M"]["max_out_deg"])),
    ]
    x = np.arange(len(rep))
    w = 0.35
    b_avg = ax2.bar(x - w/2, [t[1] for t in rep], width=w, color=C_BMSSP, label="Bậc ra trung bình $d = m/n$")
    b_max = ax2.bar(x + w/2, [t[2] for t in rep], width=w, color=C_ORANGE, label="Bậc ra cực đại $\\max\\,\\deg^+(u)$")

    for rect in b_max:
        h = rect.get_height()
        lbl_str = f"{int(round(h)):,}".replace(",", " ")
        ax2.text(rect.get_x() + rect.get_width()/2, h * 1.18, lbl_str,
                 ha="center", va="bottom", fontsize=7.4, fontweight="bold", color=C_ORANGE)

    ax2.set_yscale("log")
    ax2.set_ylim(1.0, 3.8e5)
    ax2.set_xticks(x)
    ax2.set_xticklabels([t[0] for t in rep], rotation=18)
    ax2.set_ylabel("Bậc ra của đỉnh (thang logarit)")
    ax2.set_title("(b) Chênh lệch giữa bậc trung bình và đỉnh siêu bậc")
    ax2.legend(loc="upper left", fontsize=8.0, frameon=True, facecolor="white", framealpha=0.92)

    fig.tight_layout(pad=1.2)
    fig.savefig(OUT_DIR / "ch3_dataset_landscape.png", bbox_inches="tight")
    plt.close(fig)

# ==============================================================================
# 2. ch3_synthetic_scaling.png
# ==============================================================================
def plot_fig_synthetic():
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.6, 4.25), gridspec_kw={"width_ratios": [1.12, 1.0]})

    target_ids = ["ER_1M", "SF_1M", "D12_1M", "ER_2M", "SF_2M", "D12_2M"]
    short_names = [
        "ER 1M\n(d=4)", "SF 1M\n(d=4)", "Dense 1M\n(d=12)",
        "ER 2M\n(d=4)", "SF 2M\n(d=4)", "Dense 2M\n(d=12)"
    ]
    by_id = {r["id"]: r for r in syn_rows}
    dijk_ms = [float(by_id[k]["dijkstra_1t_ms"]) for k in target_ids]
    b1_ms = [float(by_id[k]["bmssp_1t_ms"]) for k in target_ids]
    bpar_ms = [float(by_id[k]["best_par_ms"]) for k in target_ids]
    s_abs = [float(by_id[k]["speedup_vs_dijkstra"]) for k in target_ids]

    x = np.arange(len(target_ids))
    w = 0.27
    ax1.bar(x - w, dijk_ms, width=w, color=C_DIJK, label="Dijkstra tuần tự")
    ax1.bar(x, b1_ms, width=w, color=C_BMSSP_1T, label="BMSSP (1 luồng)")
    bars_par = ax1.bar(x + w, bpar_ms, width=w, color=C_TEAL, label="BMSSP song song tốt nhất")

    for rect, sp in zip(bars_par, s_abs):
        h = rect.get_height()
        ax1.text(
            rect.get_x() + rect.get_width() / 2.0,
            h + 55,
            f"{sp:.2f}x".replace(".", ","),
            ha="center", va="bottom", fontsize=8.3, fontweight="bold", color=C_TEAL
        )

    ax1.set_xticks(x)
    ax1.set_xticklabels(short_names)
    ax1.set_ylabel("Thời gian giải trung vị (ms)")
    ax1.set_ylim(0, 3050)
    ax1.set_title("(a) Đối sánh thời gian giải ở quy mô 1 - 2 triệu đỉnh")
    ax1.legend(loc="upper left", frameon=True, facecolor="white", framealpha=0.92)

    threads = [1, 2, 4, 8, 12]
    curves = [
        ("ER_100K", "ER 100K (d=4)", "#7f8c8d", "--", "o"),
        ("ER_1M", "ER 1M (d=4)", C_BMSSP, "-", "s"),
        ("SF_2M", "Scale-Free 2M (d=4)", C_ORANGE, "-.", "^"),
        ("D16_500K", "Dense 500K (d=16)", "#6f42c1", ":", "D"),
        ("D12_2M", "Dense 2M (d=12)", C_TEAL, "-", "o"),
    ]
    for cid, lbl, col, ls, mk in curves:
        r = by_id[cid]
        t1 = float(r["bmssp_1t_ms"])
        ts = [
            float(r["bmssp_1t_ms"]),
            float(r["bmssp_2t_ms"]),
            float(r["bmssp_4t_ms"]),
            float(r["bmssp_8t_ms"]),
            float(r["bmssp_12t_ms"]),
        ]
        sp_self = [t1 / t for t in ts]
        ax2.plot(threads, sp_self, label=lbl, color=col, linestyle=ls, marker=mk, linewidth=1.9, markersize=5.5)

    ax2.axhline(1.0, color="#444444", linestyle=":", linewidth=1.0)
    ax2.set_xticks(threads)
    ax2.set_xlabel("Số luồng OpenMP ($p$)")
    ax2.set_ylabel(r"Hệ số tự tăng tốc nội tại $S_{\mathrm{self}}(p)$")
    ax2.set_ylim(0.90, 2.45)
    ax2.set_title("(b) Mở rộng nội tại theo số luồng xử lý")
    ax2.legend(loc="upper left", frameon=True, facecolor="white", framealpha=0.92)

    fig.tight_layout(pad=1.2)
    fig.savefig(OUT_DIR / "ch3_synthetic_scaling.png", bbox_inches="tight")
    plt.close(fig)

# ==============================================================================
# 3. ch3_dimacs_road.png
# ==============================================================================
def plot_fig_dimacs():
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.6, 4.20), gridspec_kw={"width_ratios": [1.15, 1.0]})

    codes = [r["code"] for r in road_rows]
    n_vals = [int(r["n"]) / 1e6 for r in road_rows]
    dijk = [float(r["dijkstra_1t_ms"]) for r in road_rows]
    b1 = [float(r["bmssp_1t_ms"]) for r in road_rows]
    bpar = [float(r["best_par_ms"]) for r in road_rows]
    ratio = [float(r["ratio_bmssp_over_dijkstra"]) for r in road_rows]
    self_sp = [float(r["self_speedup"]) for r in road_rows]

    x = np.arange(len(codes))
    w = 0.27
    ax1.bar(x - w, dijk, width=w, color=C_DIJK, label="Dijkstra tuần tự")
    ax1.bar(x, b1, width=w, color=C_BMSSP_1T, label="BMSSP (1 luồng)")
    ax1.bar(x + w, bpar, width=w, color=C_BMSSP, label="BMSSP (8 - 12 luồng)")

    xlabels = [f"{c}\n({nv:.2f}M)".replace(".", ",") for c, nv in zip(codes, n_vals)]
    ax1.set_xticks(x)
    ax1.set_xticklabels(xlabels)
    ax1.set_ylabel("Thời gian giải trung vị (ms)")
    ax1.set_ylim(0, 1200)
    ax1.set_title("(a) Thời gian giải trên 7 mạng đường bộ DIMACS-9")
    ax1.legend(loc="upper left", frameon=True, facecolor="white", framealpha=0.92)

    ax2.plot(x, ratio, color=C_ORANGE, marker="o", linewidth=2.1, markersize=6,
             label=r"Tỷ số $T_{\mathrm{BMSSP}} / T_{\mathrm{Dijkstra}}$")
    ax2.plot(x, self_sp, color=C_TEAL, marker="s", linestyle="--", linewidth=1.9, markersize=5.5,
             label=r"Tự tăng tốc $S_{\mathrm{self}}$ (1T / đa luồng)")
    ax2.axhline(1.0, color="#444444", linestyle=":", linewidth=1.0, label="Ngưỡng ngang bằng (1,00x)")

    for xi, rt in zip(x, ratio):
        ax2.text(xi, rt + 0.08, f"{rt:.2f}x".replace(".", ","), ha="center", va="bottom",
                 fontsize=8.3, color=C_ORANGE, fontweight="bold")

    ax2.set_xticks(x)
    ax2.set_xticklabels(codes)
    ax2.set_xlabel("Mạng đường bộ sắp tăng dần theo số đỉnh (bậc trung bình 2,34 - 2,76)")
    ax2.set_ylabel("Tỷ số thời gian / Hệ số tăng tốc")
    ax2.set_ylim(0.85, 3.05)
    ax2.set_title("(b) Xu hướng thu hẹp khoảng cách khi tăng quy mô")
    ax2.legend(loc="upper right", frameon=True, facecolor="white", framealpha=0.92)

    fig.tight_layout(pad=1.2)
    fig.savefig(OUT_DIR / "ch3_dimacs_road.png", bbox_inches="tight")
    plt.close(fig)

# ==============================================================================
# 4. ch3_snap_highdeg.png: Giảm thời gian theo luồng & Thông lượng triệu cung/giây
# ==============================================================================
def plot_fig_snap():
    by_ds = {r["dataset"]: r for r in snap_rows}

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.6, 4.25), gridspec_kw={"width_ratios": [1.05, 1.05]})

    threads = [1, 2, 4, 8, 12]
    large_cfg = [
        ("web-BerkStan", "web-BerkStan (685K, d=11,1)", "#6f42c1", "D"),
        ("wiki-topcats", "wiki-topcats (1,79M, d=15,9)", C_BMSSP, "s"),
        ("soc-Pokec", "soc-Pokec (1,63M, d=18,8)", C_TEAL, "o"),
    ]
    for ds, lbl, col, mk in large_cfg:
        r = by_ds[ds]
        ts = [
            float(r["bmssp_1t_ms"]),
            float(r["bmssp_2t_ms"]),
            float(r["bmssp_4t_ms"]),
            float(r["bmssp_8t_ms"]),
            float(r["bmssp_12t_ms"]),
        ]
        dj = float(r["dijkstra_1t_ms"])
        ax1.plot(threads, ts, color=col, marker=mk, linewidth=2.0, markersize=5.5, label=f"BMSSP: {lbl}")
        ax1.axhline(dj, color=col, linestyle="--", linewidth=1.1, alpha=0.65)

    handles, labels = ax1.get_legend_handles_labels()
    handles.append(Line2D([0], [0], color="#444444", linestyle="--", linewidth=1.2))
    labels.append("Mức thời gian Dijkstra tuần tự tương ứng")
    ax1.set_xticks(threads)
    ax1.set_xlabel("Số luồng OpenMP ($p$)")
    ax1.set_ylabel("Thời gian giải trung vị (ms)")
    ax1.set_ylim(0, 2950)
    ax1.set_title("(a) Giảm thời gian giải theo số luồng trên mạng lớn")
    ax1.legend(handles, labels, loc="upper right", fontsize=7.9, frameon=True, facecolor="white", framealpha=0.92)

    # Ô (b): Thông lượng xử lý cung hiệu dụng (triệu cung / giây - MTEPS)
    plot_order = ["cit-HepPh", "soc-Slashdot0811", "web-BerkStan", "wiki-topcats", "soc-Pokec"]
    short_ds = [
        "cit-HepPh\n(0,42M cung)",
        "Slashdot\n(0,83M cung)",
        "BerkStan\n(7,60M cung)",
        "wiki-topcats\n(28,51M cung)",
        "soc-Pokec\n(30,62M cung)",
    ]
    mteps_dijk = [float(by_ds[k]["m"]) / (float(by_ds[k]["dijkstra_1t_ms"]) * 1e3) for k in plot_order]
    mteps_par  = [float(by_ds[k]["m"]) / (float(by_ds[k]["best_par_ms"]) * 1e3) for k in plot_order]

    x = np.arange(len(plot_order))
    w = 0.34
    b1 = ax2.bar(x - w/2, mteps_dijk, width=w, color=C_DIJK, label="Dijkstra tuần tự")
    b2 = ax2.bar(x + w/2, mteps_par, width=w, color=C_TEAL, label="BMSSP song song tốt nhất")

    for rect in b2:
        h = rect.get_height()
        ax2.text(rect.get_x() + rect.get_width()/2, h + 1.2, f"{h:.1f}".replace(".", ","),
                 ha="center", va="bottom", fontsize=7.9, fontweight="bold", color=C_TEAL)

    ax2.set_xticks(x)
    ax2.set_xticklabels(short_ds, fontsize=8.3)
    ax2.set_ylabel("Thông lượng xử lý (triệu cung / giây)")
    ax2.set_ylim(0, 74)
    ax2.set_title("(b) Thông lượng xử lý cung (MTEPS) trên 5 mạng SNAP")
    ax2.legend(loc="upper right", fontsize=8.0, frameon=True, facecolor="white", framealpha=0.92)

    fig.tight_layout(pad=1.2)
    fig.savefig(OUT_DIR / "ch3_snap_highdeg.png", bbox_inches="tight")
    plt.close(fig)

# ==============================================================================
# 5. ch3_degree_impact.png
# ==============================================================================
def plot_fig_degree_impact():
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.6, 4.20), gridspec_kw={"width_ratios": [1.0, 1.12]})

    by_road = {r["code"]: r for r in road_rows}
    by_snap = {r["dataset"]: r for r in snap_rows}
    cal = by_road["CAL"]
    pok = by_snap["soc-Pokec"]

    groups = [
        "Mạng đường bộ CAL\n(1,89M đỉnh; d = 2,44;\n4,62 triệu cung)",
        "Mạng xã hội soc-Pokec\n(1,63M đỉnh; d = 18,75;\n30,62 triệu cung)"
    ]
    dijk_v = [float(cal["dijkstra_1t_ms"]), float(pok["dijkstra_1t_ms"])]
    bmssp_v = [float(cal["best_par_ms"]), float(pok["best_par_ms"])]

    x = np.arange(2)
    w = 0.30
    b_d = ax1.bar(x - w/2, dijk_v, width=w, color=C_DIJK, label="Dijkstra tuần tự")
    b_p = ax1.bar(x + w/2, bmssp_v, width=w, color=C_TEAL, label="BMSSP song song (12 luồng)")

    for rect in b_d:
        h = rect.get_height()
        ax1.text(rect.get_x() + rect.get_width()/2, h + 45, f"{h:.1f} ms".replace(".", ","),
                 ha="center", va="bottom", fontsize=8.5, fontweight="bold", color="#495057")
    for rect in b_p:
        h = rect.get_height()
        ax1.text(rect.get_x() + rect.get_width()/2, h + 45, f"{h:.1f} ms".replace(".", ","),
                 ha="center", va="bottom", fontsize=8.5, fontweight="bold", color=C_TEAL)

    ax1.annotate(
        "Số cung tăng 6,63x:\nDijkstra chậm đi 5,12x\nBMSSP chỉ tăng 1,52x",
        xy=(0.85, 2475), xytext=(0.00, 1820),
        arrowprops=dict(arrowstyle="->", color=C_ORANGE, lw=1.4),
        fontsize=8.3, fontweight="bold", color=C_ORANGE,
        bbox=dict(boxstyle="round,pad=0.35", facecolor="#fff8f0", edgecolor=C_ORANGE, lw=0.9)
    )

    ax1.set_xticks(x)
    ax1.set_xticklabels(groups)
    ax1.set_ylabel("Thời gian giải trung vị (ms)")
    ax1.set_ylim(0, 2850)
    ax1.set_title("(a) Độ nhạy thời gian khi mật độ cạnh tăng 6,63 lần")
    ax1.legend(loc="upper left", frameon=True, facecolor="white", framealpha=0.92)

    iso_slices = [
        ("Thực tế n ≈ 1,63M - 1,89M (CAL → Wiki → Pokec)", [2.44, 15.91, 18.75], [1/1.846, 1.688, 1.831], C_BMSSP, "o", "-"),
        ("Tổng hợp đẳng đỉnh n = 2,0M (ER → Dense-12)", [4.00, 12.00], [1.6484, 2.1998], C_TEAL, "D", "--"),
        ("Tổng hợp đẳng đỉnh n = 1,0M (ER → Dense-12)", [4.00, 12.00], [1.4975, 1.8221], C_ORANGE, "^", "--"),
        ("Tổng hợp đẳng đỉnh n = 500K (ER → Dense-16)", [4.00, 16.00], [1.3875, 1.6943], "#6f42c1", "s", "-."),
        ("Tổng hợp đẳng đỉnh n = 200K (ER → Dense-20)", [4.01, 20.00], [1.0978, 1.5544], "#7f8c8d", "v", ":"),
    ]
    for lbl, ds, sps, col, mk, ls in iso_slices:
        ax2.plot(ds, sps, label=lbl, color=col, marker=mk, linestyle=ls, linewidth=2.0, markersize=5.8)

    ax2.axhline(1.0, color=C_RED, linestyle=":", linewidth=1.2, label="Ngưỡng vượt Dijkstra (1,00x)")
    ax2.set_xlabel("Bậc ra trung bình $d = m / n$")
    ax2.set_ylabel(r"Hệ số tăng tốc $S_{\mathrm{abs}}$ so với Dijkstra")
    ax2.set_xlim(1.5, 21.2)
    ax2.set_ylim(0.35, 2.38)
    ax2.set_title("(b) Lát cắt đẳng đỉnh theo bậc ra trung bình d")
    ax2.legend(loc="lower right", fontsize=7.6, frameon=True, facecolor="white", framealpha=0.92)

    fig.tight_layout(pad=1.2)
    fig.savefig(OUT_DIR / "ch3_degree_impact.png", bbox_inches="tight")
    plt.close(fig)

# ==============================================================================
# 6. ch3_phase_diagram_nd.png
# ==============================================================================
def plot_fig_phase_nd():
    pts = []
    for r in syn_rows:
        n = float(r["n"])
        d = float(r["m"]) / n
        sp = float(r["speedup_vs_dijkstra"])
        pts.append(("Tổng hợp kiểm soát", "syn", r["id"], n, d, sp))
    for r in road_rows:
        n = float(r["n"])
        d = float(r["m"]) / n
        sp = 1.0 / float(r["ratio_bmssp_over_dijkstra"])
        pts.append(("Đường bộ DIMACS-9", "road", r["code"], n, d, sp))
    for r in snap_rows:
        n = float(r["n"])
        d = float(r["m"]) / n
        sp = float(r["speedup_vs_dijkstra"])
        pts.append(("Mạng thực tế SNAP", "snap", r["dataset"], n, d, sp))

    X = np.array([[1.0, math.log10(p[3]), math.log10(p[4]), 1.0 if p[1] == "road" else 0.0] for p in pts])
    Y = np.array([math.log(p[5]) for p in pts])
    beta, *_ = np.linalg.lstsq(X, Y, rcond=None)
    Y_hat = X @ beta
    r2 = 1.0 - np.sum((Y - Y_hat) ** 2) / np.sum((Y - np.mean(Y)) ** 2)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.6, 4.35), gridspec_kw={"width_ratios": [1.22, 0.95]})

    grid_n = np.logspace(np.log10(26_000), np.log10(3_200_000), 220)
    grid_d = np.linspace(1.6, 22.0, 220)
    GN, GD = np.meshgrid(grid_n, grid_d)
    road_weight = np.clip((3.6 - GD) / 1.2, 0.0, 1.0)
    Z_sp = np.exp(beta[0] + beta[1] * np.log10(GN) + beta[2] * np.log10(GD) + beta[3] * road_weight)

    norm = TwoSlopeNorm(vmin=0.32, vcenter=1.0, vmax=2.25)
    ax1.contourf(GN, GD, Z_sp, levels=np.linspace(0.32, 2.25, 25), cmap="RdYlGn", norm=norm, alpha=0.28)
    cs = ax1.contour(GN, GD, Z_sp, levels=[1.0], colors=[C_RED], linewidths=2.0, linestyles="--")
    ax1.clabel(cs, fmt={1.0: " Ranh giới hòa vốn (1,00x) "}, fontsize=8.2, inline=True, manual=[(182_000, 14.5)])

    cat_style = {
        "Tổng hợp kiểm soát": ("o", C_BMSSP),
        "Đường bộ DIMACS-9": ("^", C_ORANGE),
        "Mạng thực tế SNAP": ("s", C_TEAL),
    }

    for cat, (mk, col) in cat_style.items():
        sub = [p for p in pts if p[0] == cat]
        ax1.scatter(
            [p[3] for p in sub], [p[4] for p in sub],
            marker=mk, s=56, color=col, edgecolors="white", linewidth=0.7, zorder=4, label=cat
        )

    highlight_offsets = {
        "cit-HepPh": (7, 4),
        "soc-Slashdot0811": (-34, -14),
        "web-BerkStan": (-34, -14),
        "soc-Pokec": (-64, 7),
        "wiki-topcats": (-72, -13),
        "NY": (7, 2),
        "NE": (-18, 9),
        "CAL": (6, -9),
        "ER_100K": (-18, 8),
        "ER_200K": (6, 6),
        "ER_2M": (-22, 8),
        "D12_2M": (-38, -14),
        "D20_200K": (7, -4),
    }
    for cat, fam, name, n, d, sp in pts:
        if name in highlight_offsets:
            dx, dy = highlight_offsets[name]
            short = name.replace("soc-", "")
            ax1.annotate(
                f"{short} ({sp:.2f}x)".replace(".", ","),
                xy=(n, d), xytext=(dx, dy), textcoords="offset points",
                fontsize=7.5, fontweight="bold" if sp >= 1.0 else "normal",
                color="#1b4332" if sp >= 1.0 else "#495057", zorder=5
            )

    ax1.set_xscale("log")
    ax1.set_xlim(2.6e4, 3.3e6)
    ax1.set_ylim(1.6, 22.0)
    ax1.set_xlabel("Số đỉnh $n$ (thang logarit)")
    ax1.set_ylabel("Bậc ra trung bình $d = m / n$")
    ax1.set_title("(a) Bản đồ phân vùng ưu thế trên mặt phẳng (n, d)")
    ax1.legend(loc="upper left", fontsize=7.9, frameon=True, facecolor="white", framealpha=0.92)

    sp_actual = np.exp(Y)
    sp_pred = np.exp(Y_hat)
    for cat, (mk, col) in cat_style.items():
        idx = [i for i, p in enumerate(pts) if p[0] == cat]
        ax2.scatter(
            [sp_pred[i] for i in idx], [sp_actual[i] for i in idx],
            marker=mk, s=52, color=col, edgecolors="white", linewidth=0.7, label=cat
        )

    lim = [0.28, 2.45]
    ax2.plot(lim, lim, color="#333333", linestyle="--", linewidth=1.2, label="Đường khớp lý tưởng y = x")
    ax2.axhline(1.0, color=C_RED, linestyle=":", linewidth=1.0, alpha=0.7)
    ax2.axvline(1.0, color=C_RED, linestyle=":", linewidth=1.0, alpha=0.7)
    ax2.text(
        0.33, 2.06,
        f"Mô hình hồi quy 23 bộ dữ liệu:\nR² = {r2:.4f}".replace(".", ","),
        fontsize=8.6, fontweight="bold", color=C_BMSSP,
        bbox=dict(boxstyle="round,pad=0.38", facecolor="#f0f6ff", edgecolor=C_BMSSP, lw=0.9)
    )
    ax2.set_xlim(lim)
    ax2.set_ylim(lim)
    ax2.set_xlabel(r"$S_{\mathrm{abs}}$ dự báo từ mô hình $(n, d)$")
    ax2.set_ylabel(r"$S_{\mathrm{abs}}$ thực đo trên phần cứng")
    ax2.set_title("(b) Độ khớp của quy luật thực nghiệm (n, d)")
    ax2.legend(loc="lower right", fontsize=7.8, frameon=True, facecolor="white", framealpha=0.92)

    fig.tight_layout(pad=1.2)
    fig.savefig(OUT_DIR / "ch3_phase_diagram_nd.png", bbox_inches="tight")
    plt.close(fig)
    print(f"Generated all 6 PNG charts successfully in {OUT_DIR} (R^2 = {r2:.4f})")

if __name__ == "__main__":
    plot_fig_dataset_landscape()
    plot_fig_synthetic()
    plot_fig_dimacs()
    plot_fig_snap()
    plot_fig_degree_impact()
    plot_fig_phase_nd()
