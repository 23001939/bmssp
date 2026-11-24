import csv
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

with open("bmssp_parallel/snap_highdeg_results.csv", "r", encoding="utf-8") as f:
    rows = list(csv.DictReader(f))

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13.5, 5.2), dpi=160)

# Panel 1: Thread scaling vs Dijkstra 1T on wiki-topcats & soc-Pokec
threads = ["Dijkstra\n1T", "BMSSP\n1T", "BMSSP\n2T", "BMSSP\n4T", "BMSSP\n8T", "BMSSP\n12T"]
x = np.arange(len(threads))
w = 0.35

colors = ["#2563eb", "#059669"]
for idx, r in enumerate(rows):
    vals = [
        float(r["dijkstra_1t_ms"]),
        float(r["bmssp_1t_ms"]),
        float(r["bmssp_2t_ms"]),
        float(r["bmssp_4t_ms"]),
        float(r["bmssp_8t_ms"]),
        float(r["bmssp_12t_ms"]),
    ]
    offset = -w / 2 if idx == 0 else w / 2
    lbl = f"{r['dataset']} (n={int(r['n'])/1e6:.2f}M, deg={float(r['avg_deg']):.2f})"
    bars = ax1.bar(x + offset, vals, w, label=lbl, color=colors[idx], alpha=0.9)
    for b in bars:
        ax1.annotate(
            f"{b.get_height():.0f}",
            (b.get_x() + b.get_width() / 2, b.get_height()),
            textcoords="offset points",
            xytext=(0, 4),
            ha="center",
            fontsize=7.8,
        )

ax1.set_xticks(x)
ax1.set_xticklabels(threads)
ax1.set_ylabel("Thời gian thực thi trung vị (ms)")
ax1.set_title("Hiệu năng trên dữ liệu thực Stanford SNAP (bậc 15.9 – 18.8)")
ax1.set_ylim(0, 2900)
ax1.grid(axis="y", linestyle="--", alpha=0.35)
ax1.legend(fontsize=8.5)

# Panel 2: Contrast Road Network CAL (deg=2.44) vs SNAP High-Degree (deg=15.91, 18.75) at ~1.7M nodes
labels = [
    "DIMACS CAL\n(1.89M đỉnh, bậc 2.44)",
    "SNAP wiki-topcats\n(1.79M đỉnh, bậc 15.91)",
    "SNAP soc-Pokec\n(1.63M đỉnh, bậc 18.75)",
]
dijk_cmp = [482.934, float(rows[0]["dijkstra_1t_ms"]), float(rows[1]["dijkstra_1t_ms"])]
bmssp_cmp = [891.428, float(rows[0]["best_par_ms"]), float(rows[1]["best_par_ms"])]

x2 = np.arange(len(labels))
b_d = ax2.bar(x2 - w / 2, dijk_cmp, w, label="Dijkstra 1T", color="#dc2626", alpha=0.88)
b_p = ax2.bar(x2 + w / 2, bmssp_cmp, w, label="BMSSP Tối ưu (12T)", color="#059669", alpha=0.92)

for i in range(len(labels)):
    sp = dijk_cmp[i] / bmssp_cmp[i]
    top = max(dijk_cmp[i], bmssp_cmp[i])
    tag = f"BMSSP nhanh hơn {sp:.2f}x" if sp >= 1.0 else f"Dijkstra nhanh hơn {1/sp:.2f}x"
    ax2.annotate(tag, (x2[i], top), textcoords="offset points", xytext=(0, 8), ha="center", fontsize=8.5, fontweight="bold")

ax2.set_xticks(x2)
ax2.set_xticklabels(labels, fontsize=8.5)
ax2.set_ylabel("Thời gian thực thi trung vị (ms)")
ax2.set_title("Tác động của bậc trung bình ở cùng quy mô ~1.6M–1.9M đỉnh")
ax2.set_ylim(0, 2900)
ax2.grid(axis="y", linestyle="--", alpha=0.35)
ax2.legend(fontsize=8.5)

plt.tight_layout()
plt.savefig("bmssp_parallel/snap_high_degree_vs_dijkstra.png")
print("Saved snap_high_degree_vs_dijkstra.png OK")
