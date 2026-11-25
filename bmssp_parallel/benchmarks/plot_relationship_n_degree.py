import csv
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.colors import TwoSlopeNorm

# 1. Load all 23 empirical benchmarks
syn_rows = list(csv.DictReader(open("bmssp_parallel/final_benchmark_1m_2m.csv", encoding="utf-8")))
road_rows = list(csv.DictReader(open("bmssp_parallel/dimacs_road_7_results.csv", encoding="utf-8")))
snap_rows = list(csv.DictReader(open("bmssp_parallel/snap_highdeg_results.csv", encoding="utf-8")))

points = []
for r in syn_rows:
    n = int(r["n"])
    m = int(r["m"])
    d = m / n
    points.append({
        "id": r["id"], "group": "Tổng hợp ngẫu nhiên (ER / Scale-Free / Dense)",
        "family": r["family"], "n": n, "m": m, "deg": d,
        "dijk": float(r["dijkstra_1t_ms"]), "bmssp": float(r["best_par_ms"]),
        "speedup": float(r["speedup_vs_dijkstra"]),
        "self_scale": float(r["self_speedup_1t_best"]), "marker": "s"
    })

for r in road_rows:
    n = int(r["n"])
    m = int(r["m"])
    d = m / n
    dijk = float(r["dijkstra_1t_ms"])
    bmssp = float(r["best_par_ms"])
    points.append({
        "id": r["code"], "group": "Thực tế: Đường bộ DIMACS-9 (Phẳng 2D)",
        "family": "road", "n": n, "m": m, "deg": d,
        "dijk": dijk, "bmssp": bmssp,
        "speedup": dijk / bmssp, "self_scale": float(r["self_speedup"]), "marker": "^"
    })

for r in snap_rows:
    n = int(r["n"])
    m = int(r["m"])
    d = m / n
    points.append({
        "id": r["dataset"], "group": "Thực tế: Web / Xã hội / Trích dẫn SNAP",
        "family": "snap", "n": n, "m": m, "deg": d,
        "dijk": float(r["dijkstra_1t_ms"]), "bmssp": float(r["best_par_ms"]),
        "speedup": float(r["speedup_vs_dijkstra"]),
        "self_scale": float(r["self_speedup"]), "marker": "o"
    })

by_id = {p["id"]: p for p in points}

# Fit smooth empirical response surface: log(speedup) ~ b0 + b1*log10(n) + b2*log10(deg) + b3*is_road
X, Y = [], []
for p in points:
    is_road = 1.0 if p["family"] == "road" else 0.0
    X.append([1.0, np.log10(p["n"]), np.log10(p["deg"]), is_road])
    Y.append(np.log(p["speedup"]))
X = np.array(X)
Y = np.array(Y)
beta, *_ = np.linalg.lstsq(X, Y, rcond=None)
r2 = 1.0 - np.sum((Y - X @ beta) ** 2) / np.sum((Y - np.mean(Y)) ** 2)

plt.rcParams["font.sans-serif"] = ["Segoe UI", "DejaVu Sans", "Arial"]
plt.rcParams["axes.unicode_minus"] = False

fig, axes = plt.subplots(2, 2, figsize=(16.6, 11.8), dpi=175)
(axA, axB), (axC, axD) = axes

# ==============================================================================
# PANEL A: 2D Phase Diagram (n vs average degree d)
# ==============================================================================
grid_n = np.logspace(np.log10(26_000), np.log10(3_300_000), 220)
grid_d = np.linspace(1.6, 21.6, 220)
GN, GD = np.meshgrid(grid_n, grid_d)
road_weight = np.clip((3.6 - GD) / 1.2, 0.0, 1.0)
Z_speedup = np.exp(beta[0] + beta[1] * np.log10(GN) + beta[2] * np.log10(GD) + beta[3] * road_weight)

norm = TwoSlopeNorm(vmin=0.32, vcenter=1.0, vmax=2.25)
cf = axA.contourf(GN, GD, Z_speedup, levels=np.linspace(0.32, 2.25, 25), cmap="RdYlGn", norm=norm, alpha=0.30)
cs = axA.contour(GN, GD, Z_speedup, levels=[1.0], colors=["#b91c1c"], linewidths=2.2, linestyles="--")
axA.clabel(cs, fmt={1.0: "Ranh giới hòa vốn (1.00x)"}, fontsize=8.8, inline=True, manual=[(190_000, 11.5)])

seen_grp = set()
for p in points:
    lbl = p["group"] if p["group"] not in seen_grp else ""
    seen_grp.add(p["group"])
    axA.scatter(
        p["n"], p["deg"],
        c=[p["speedup"]], cmap="RdYlGn", norm=norm,
        marker=p["marker"], s=105, edgecolors="#0f172a", linewidths=1.1, zorder=5, label=lbl
    )

annot_cfg = {
    "cit-HepPh": ("cit-HepPh (0.35x)", (8, 5)),
    "soc-Slashdot0811": ("Slashdot (0.82x)", (8, -11)),
    "web-BerkStan": ("web-BerkStan (1.03x)", (-38, -15)),
    "wiki-topcats": ("wiki-topcats (1.69x)", (-84, -4)),
    "soc-Pokec": ("soc-Pokec (1.83x)", (-76, 8)),
    "ER_100K": ("ER 100K (0.90x)", (-26, 9)),
    "ER_200K": ("ER 200K (1.10x)", (6, 8)),
    "D20_200K": ("Dense-20 200K (1.55x)", (9, -4)),
    "D16_500K": ("Dense-16 500K (1.69x)", (-38, 9)),
    "ER_1M": ("ER 1M (1.50x)", (-28, 9)),
    "D12_1M": ("Dense-12 1M (1.82x)", (-64, 9)),
    "ER_2M": ("ER 2M (1.65x)", (-26, 9)),
    "D12_2M": ("Dense-12 2M (2.20x)", (-22, -15)),
    "NY": ("NY (0.38x)", (-16, -14)),
    "FLA": ("FLA (0.42x)", (-34, -14)),
    "NE": ("NE (0.59x)", (-28, 9)),
    "CAL": ("CAL (0.54x)", (7, -8)),
}
for p in points:
    if p["id"] in annot_cfg:
        txt, xyoff = annot_cfg[p["id"]]
        axA.annotate(txt, (p["n"], p["deg"]), textcoords="offset points", xytext=xyoff, fontsize=7.8, fontweight="semibold", color="#0f172a")

cbar = fig.colorbar(cf, ax=axA, pad=0.02)
cbar.set_label("Hệ số tăng tốc: T_Dijkstra / T_BMSSP (>1.0x: BMSSP thắng)", fontsize=8.8)

axA.set_xscale("log")
axA.set_xlim(26_000, 3_600_000)
axA.set_ylim(1.4, 25.0)
axA.set_xlabel("Số đỉnh n (thang log)", fontsize=9.5, fontweight="bold")
axA.set_ylabel("Bậc ra trung bình d = m / n", fontsize=9.5, fontweight="bold")
axA.set_title(f"A. Bản đồ pha (n, d): Vùng thắng của Dijkstra vs. Parallel BMSSP (R² = {r2:.2f})", fontsize=10.5, fontweight="bold")
axA.grid(True, which="both", linestyle=":", alpha=0.35)
axA.legend(loc="upper left", fontsize=8.0, framealpha=0.92)

# ==============================================================================
# PANEL B: Effect of Vertex Count (n) at Fixed Degree Regimes
# ==============================================================================
er4 = sorted([p for p in points if p["family"] == "er" and abs(p["deg"] - 4.0) < 0.5], key=lambda x: x["n"])
dense_syn = sorted([p for p in points if p["id"] in ("D20_200K", "D16_500K", "D12_1M", "D12_2M")], key=lambda x: x["n"])
snap_s = sorted([by_id[k] for k in ("cit-HepPh", "soc-Slashdot0811", "web-BerkStan", "soc-Pokec", "wiki-topcats")], key=lambda x: x["n"])
road_s = sorted([p for p in points if p["family"] == "road"], key=lambda x: x["n"])

axB.plot([p["n"] for p in dense_syn], [p["speedup"] for p in dense_syn], "s-", lw=2.3, color="#047857", label="Tổng hợp bậc cao (d = 12 – 20)")
axB.plot([p["n"] for p in snap_s], [p["speedup"] for p in snap_s], "o-", lw=2.3, color="#2563eb", label="Thực tế SNAP Web/Xã hội (d = 10.7 – 18.8)")
axB.plot([p["n"] for p in er4], [p["speedup"] for p in er4], "D--", lw=2.0, color="#7c3aed", label="Ngẫu nhiên thưa Erdős–Rényi (d = 4.0)")
axB.plot([p["n"] for p in road_s], [p["speedup"] for p in road_s], "^-.", lw=2.0, color="#dc2626", label="Thực tế Đường bộ DIMACS-9 (d = 2.4 – 2.7)")

axB.axhline(1.0, color="#0f172a", linestyle=":", lw=1.6, label="Ngưỡng hòa vốn (1.00x)")
axB.axvspan(350_000, 1_000_000, color="#fef08a", alpha=0.22, label="Vùng chuyển tiếp tràn L3 Cache (16 MB)")

axB.set_xscale("log")
axB.set_xlim(26_000, 2_600_000)
axB.set_ylim(0.25, 2.35)
axB.set_xlabel("Số đỉnh n (thang log)", fontsize=9.5, fontweight="bold")
axB.set_ylabel("Hệ số tăng tốc: T_Dijkstra / T_BMSSP", fontsize=9.5, fontweight="bold")
axB.set_title("B. Quan hệ theo Số đỉnh (n): Tỷ số BMSSP thắng tăng đơn điệu khi n vượt L3 Cache", fontsize=10.5, fontweight="bold")
axB.grid(True, which="both", linestyle="--", alpha=0.35)
axB.legend(loc="upper left", fontsize=8.0, framealpha=0.92)

# ==============================================================================
# PANEL C: Controlled Effect of Average Degree (d) at Fixed Vertex Scales (Iso-n)
# ==============================================================================
real_17m = [by_id["CAL"], by_id["wiki-topcats"], by_id["soc-Pokec"]]
syn_2m = [by_id["ER_2M"], by_id["D12_2M"]]
syn_1m = [by_id["ER_1M"], by_id["D12_1M"]]
syn_500k = [by_id["ER_500K"], by_id["D16_500K"]]
syn_200k = [by_id["ER_200K"], by_id["D20_200K"]]

axC.plot([p["deg"] for p in real_17m], [p["speedup"] for p in real_17m], "o-", lw=2.5, color="#2563eb",
         label="Đồ thị thực tế n ≈ 1.63M – 1.89M (CAL → wiki-topcats → soc-Pokec)")
axC.plot([p["deg"] for p in syn_2m], [p["speedup"] for p in syn_2m], "s--", lw=2.2, color="#047857",
         label="Tổng hợp đẳng đỉnh n = 2.0M (ER d=4 → Dense d=12)")
axC.plot([p["deg"] for p in syn_1m], [p["speedup"] for p in syn_1m], "D--", lw=2.0, color="#0d9488",
         label="Tổng hợp đẳng đỉnh n = 1.0M (ER d=4 → Dense d=12)")
axC.plot([p["deg"] for p in syn_500k], [p["speedup"] for p in syn_500k], "^-.", lw=1.9, color="#7c3aed",
         label="Tổng hợp đẳng đỉnh n = 500K (ER d=4 → Dense d=16)")
axC.plot([p["deg"] for p in syn_200k], [p["speedup"] for p in syn_200k], "v:", lw=1.9, color="#d97706",
         label="Tổng hợp đẳng đỉnh n = 200K (ER d=4 → Dense d=20)")

axC.axhline(1.0, color="#dc2626", linestyle=":", lw=1.6)
c_annots = [
    (by_id["CAL"], "CAL (0.54x)", (8, -4)),
    (by_id["wiki-topcats"], "wiki-topcats (1.69x)", (-28, -16)),
    (by_id["soc-Pokec"], "soc-Pokec (1.83x)", (-36, 9)),
    (by_id["ER_2M"], "ER 2M (1.65x)", (6, 8)),
    (by_id["D12_2M"], "Dense-12 2M (2.20x)", (-10, 9)),
    (by_id["D12_1M"], "Dense-12 1M (1.82x)", (8, -4)),
    (by_id["D16_500K"], "Dense-16 500K (1.69x)", (-18, 10)),
    (by_id["D20_200K"], "Dense-20 200K (1.55x)", (-64, -15)),
]
for p, txt, off in c_annots:
    axC.annotate(txt, (p["deg"], p["speedup"]), textcoords="offset points", xytext=off, fontsize=7.9, fontweight="semibold")

axC.set_xlim(1.5, 21.5)
axC.set_ylim(0.30, 2.42)
axC.set_xlabel("Bậc ra trung bình d = m / n", fontsize=9.5, fontweight="bold")
axC.set_ylabel("Hệ số tăng tốc: T_Dijkstra / T_BMSSP", fontsize=9.5, fontweight="bold")
axC.set_title("C. Quan hệ đẳng đỉnh theo Bậc (d): Tăng bậc luôn khuếch đại lợi thế của BMSSP", fontsize=10.5, fontweight="bold")
axC.grid(True, linestyle="--", alpha=0.35)
axC.legend(loc="lower right", fontsize=7.8, framealpha=0.92)

# ==============================================================================
# PANEL D: Parallel Self-Scaling Efficiency vs Degree & Scale
# ==============================================================================
large_pts = [p for p in points if p["n"] >= 200_000]
for grp, col, mk in [
    ("Thực tế: Đường bộ DIMACS-9 (Phẳng 2D)", "#dc2626", "^"),
    ("Tổng hợp ngẫu nhiên (ER / Scale-Free / Dense)", "#047857", "s"),
    ("Thực tế: Web / Xã hội / Trích dẫn SNAP", "#2563eb", "o"),
]:
    sub = [p for p in large_pts if p["group"] == grp]
    axD.scatter(
        [p["deg"] for p in sub], [p["self_scale"] for p in sub],
        s=[50 + 95 * (p["n"] / 2_000_000) for p in sub],
        color=col, marker=mk, alpha=0.88, edgecolors="#0f172a", label=grp
    )

d_annots = {
    "NE": ("NE (1.23x)", (8, 5)),
    "FLA": ("FLA (1.18x)", (8, -11)),
    "ER_2M": ("ER 2M (1.88x)", (8, 4)),
    "D12_1M": ("Dense-12 1M (2.12x)", (8, -3)),
    "D12_2M": ("Dense-12 2M (2.28x)", (8, 3)),
    "D16_500K": ("Dense-16 500K (2.04x)", (8, 6)),
    "D20_200K": ("Dense-20 200K (1.84x)", (-58, -14)),
    "web-BerkStan": ("web-BerkStan (1.60x)", (8, -4)),
    "wiki-topcats": ("wiki-topcats (1.93x)", (-68, -14)),
    "soc-Pokec": ("soc-Pokec (1.86x)", (-56, 8)),
}
for p in large_pts:
    if p["id"] in d_annots:
        txt, off = d_annots[p["id"]]
        axD.annotate(txt, (p["deg"], p["self_scale"]), textcoords="offset points", xytext=off, fontsize=7.8, fontweight="semibold")

degs_arr = np.array([p["deg"] for p in large_pts])
ss_arr = np.array([p["self_scale"] for p in large_pts])
coef_ss = np.polyfit(np.log(degs_arr), ss_arr, 1)
dx_line = np.linspace(2.2, 20.5, 100)
axD.plot(dx_line, coef_ss[0] * np.log(dx_line) + coef_ss[1], "--", color="#475569", lw=1.6, label="Xu hướng tăng độ lợi đa luồng theo log(d)")

axD.set_xlim(1.5, 21.8)
axD.set_ylim(0.95, 2.45)
axD.set_xlabel("Bậc ra trung bình d = m / n (kích thước điểm tỷ lệ với số đỉnh n)", fontsize=9.5, fontweight="bold")
axD.set_ylabel("Tăng tốc nội tại đa luồng: BMSSP 1T / Best Par", fontsize=9.5, fontweight="bold")
axD.set_title("D. Cơ chế vật lý: Bậc d càng cao, hiệu suất khai thác đa luồng (8–12T) càng mạnh", fontsize=10.5, fontweight="bold")
axD.grid(True, linestyle="--", alpha=0.35)
axD.legend(loc="lower right", fontsize=8.0, framealpha=0.92)

fig.suptitle(
    "Tổng Hợp Quy Luật Thực Nghiệm Giữa Số Đỉnh (n), Bậc Đỉnh (d) Với Hiệu Năng Dijkstra & Parallel BMSSP\n"
    "(23 bộ dữ liệu từ 34.5K đến 2.0M đỉnh, lên tới 30.6M cung — AMD Ryzen 5 7640HS 6C/12T, MaxDiff = 0.000 trên 100% phép đo)",
    fontsize=12.2, fontweight="bold", y=0.992
)
plt.tight_layout(rect=[0, 0, 1, 0.962])
plt.savefig("bmssp_parallel/relationship_n_degree_bmssp_vs_dijkstra.png")
print("Saved polished relationship_n_degree_bmssp_vs_dijkstra.png OK")
