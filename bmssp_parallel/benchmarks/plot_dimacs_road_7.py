import os
import csv
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

if os.path.exists('bmssp_parallel/benchmarks/inspect_skewed.py'):
    os.remove('bmssp_parallel/benchmarks/inspect_skewed.py')

with open('bmssp_parallel/dimacs_road_7_results.csv', 'r', encoding='utf-8') as f:
    rows = list(csv.DictReader(f))

codes = [r['code'] for r in rows]
dijk = [float(r['dijkstra_1t_ms']) for r in rows]
bmssp_1t = [float(r['bmssp_1t_ms']) for r in rows]
bmssp_par = [float(r['best_par_ms']) for r in rows]
ratios = [float(r['ratio_bmssp_over_dijkstra']) for r in rows]

dijk_est_lo = [45, 55, 75, 200, 220, 310, 380]
dijk_est_hi = [60, 70, 95, 260, 285, 390, 480]
bmssp_est_lo = [95, 105, 125, 320, 350, 460, 560]
bmssp_est_hi = [125, 140, 160, 410, 450, 580, 710]

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13.5, 5.2), dpi=160)
x = np.arange(len(codes))
w = 0.27

ax1.bar(x - w, dijk, w, label='Dijkstra 1T (Thực tế)', color='#2563eb')
ax1.bar(x, bmssp_par, w, label='BMSSP Tối ưu Đa luồng (Thực tế)', color='#059669')
ax1.bar(x + w, bmssp_1t, w, label='BMSSP Tối ưu 1T (Thực tế)', color='#94a3b8')

for i in range(len(codes)):
    lbl = 'Khoảng ước tính trước đo' if i == 0 else ''
    ax1.plot([i - w, i - w], [dijk_est_lo[i], dijk_est_hi[i]], color='#f59e0b', lw=2.5, marker='_', ms=8, label=lbl)
    ax1.plot([i, i], [bmssp_est_lo[i], bmssp_est_hi[i]], color='#f59e0b', lw=2.5, marker='_', ms=8)

ax1.set_xticks(x)
ax1.set_xticklabels(codes)
ax1.set_ylabel('Thời gian thực thi trung vị (ms)')
ax1.set_title('Thời gian chạy thực tế vs. Ước tính (NY -> CAL)')
ax1.grid(axis='y', linestyle='--', alpha=0.35)
ax1.legend(fontsize=8.5)

ax2.plot(x, ratios, marker='o', lw=2.4, color='#dc2626', label='Tỷ số thực tế: BMSSP Tốt nhất / Dijkstra 1T')
for i, v in enumerate(ratios):
    ax2.annotate(f'{v:.2f}x', (x[i], v), textcoords='offset points', xytext=(0, 8), ha='center', fontsize=9, fontweight='bold')
ax2.axhline(1.0, color='#1e293b', linestyle=':', lw=1.2, label='Ngưỡng hòa vốn (1.00x)')
ax2.set_xticks(x)
ax2.set_xticklabels([f"{r['code']}\n({int(r['n'])/1e6:.2f}M)" for r in rows])
ax2.set_ylim(0.8, 3.1)
ax2.set_ylabel('Tỷ số BMSSP / Dijkstra (càng thấp càng tốt)')
ax2.set_title('Xu hướng thu hẹp khoảng cách khi tăng quy mô mạng đường bộ')
ax2.grid(True, linestyle='--', alpha=0.35)
ax2.legend(fontsize=8.5)

plt.tight_layout()
plt.savefig('bmssp_parallel/dimacs_road_7_vs_estimate.png')
print('Saved dimacs_road_7_vs_estimate.png OK')
