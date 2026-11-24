import csv
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

with open("bmssp_parallel/snap_highdeg_results.csv", "r", encoding="utf-8") as f:
    raw_rows = {r["dataset"]: r for r in csv.DictReader(f)}

order = ["cit-HepPh", "soc-Slashdot0811", "web-BerkStan", "wiki-topcats", "soc-Pokec"]
rows = [raw_rows[k] for k in order if k in raw_rows]

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14.0, 5.4), dpi=160)

# Panel 1: Speedup curve (Dijkstra 1T / Best BMSSP) across scale 34.5K -> 1.79M
x = np.arange(len(rows))
speedups = [float(r["speedup_vs_dijkstra"]) for r in rows]
self_scales = [float(r["self_speedup"]) for r in rows]
xlabels = [
    f"{r['dataset']}\n(n={int(r['n'])/1e3:.0f}K, deg={float(r['avg_deg']):.1f})"
    if int(r["n"]) < 1_000_000
    else f"{r['dataset']}\n(n={int(r['n'])/1e6:.2f}M, deg={float(r['avg_deg']):.1f})"
    for r in rows
]

ax1.plot(x, speedups, marker="o", lw=2.5, color="#059669", label="Speedup vs. Dijkstra 1T (Dijk / BMSSP)")
ax1.plot(x, self_scales, marker="s", lw=2.0, linestyle="--", color="#2563eb", label="Tăng tốc nội tại BMSSP (1T / Best)")
ax1.axhline(1.0, color="#dc2626", linestyle=":", lw=1.5, label="Ngưỡng hòa vốn Dijkstra (1.00x)")

for i, v in enumerate(speedups):
    ax1.annotate(
        f"{v:.2f}x",
        (x[i], v),
        textcoords="offset points",
        xytext=(0, 9),
        ha="center",
        fontsize=9,
        fontweight="bold",
        color="#065f46" if v >= 1.0 else "#991b1b",
    )

ax1.set_xticks(x)
ax1.set_xticklabels(xlabels, fontsize=8.3)
ax1.set_ylim(0.15, 2.25)
ax1.set_ylabel("Hệ số tăng tốc (lớn hơn 1.0x là nhanh hơn Dijkstra)")
ax1.set_title("Đường cong hội tụ & điểm cắt vượt Dijkstra trên 5 đồ thị SNAP")
ax1.grid(True, linestyle="--", alpha=0.35)
ax1.legend(fontsize=8.3, loc="upper left")

# Panel 2: Log-scale runtime comparison (Dijkstra 1T vs BMSSP 1T vs BMSSP Best Par)
w = 0.26
dijk = [float(r["dijkstra_1t_ms"]) for r in rows]
b1 = [float(r["bmssp_1t_ms"]) for r in rows]
bpar = [float(r["best_par_ms"]) for r in rows]

ax2.bar(x - w, dijk, w, label="Dijkstra 1T", color="#dc2626", alpha=0.88)
ax2.bar(x, bpar, w, label="BMSSP Tối ưu Đa luồng (Tốt nhất)", color="#059669", alpha=0.92)
ax2.bar(x + w, b1, w, label="BMSSP Tối ưu 1T", color="#94a3b8", alpha=0.85)

ax2.set_yscale("log")
ax2.set_xticks(x)
ax2.set_xticklabels([r["dataset"] for r in rows], fontsize=8.5, rotation=10)
ax2.set_ylabel("Thời gian thực thi trung vị (ms, thang log)")
ax2.set_title("Thời gian chạy thực tế trên 5 bộ dữ liệu Stanford SNAP")
ax2.grid(axis="y", which="both", linestyle="--", alpha=0.35)
ax2.legend(fontsize=8.3)

plt.tight_layout()
plt.savefig("bmssp_parallel/snap_high_degree_vs_dijkstra.png")
print("Saved updated 5-dataset snap_high_degree_vs_dijkstra.png OK")
