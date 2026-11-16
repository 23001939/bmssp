"""Generate dataset files and offline HTML exports for BMSSP Mizchi Lab (M5)."""
from __future__ import annotations

import csv
import io
import json
import math
import os
import re
import sys
import time
import zipfile
from pathlib import Path
from typing import Any, Dict, List

ROOT = Path(__file__).resolve().parent
DATA_DIR = ROOT / 'data'
EXPORTS_DIR = ROOT / 'exports'
DIST_DIR = ROOT / 'dist'

DATA_DIR.mkdir(parents=True, exist_ok=True)
EXPORTS_DIR.mkdir(parents=True, exist_ok=True)

try:
    from data import (
        demo_graph,
        weighted_graph_50,
        weighted_graph_100,
        graph_to_txt,
        verify_result,
    )
    from runner import run_batch, launch_traced_process
except ImportError:
    from bmssp_visualizer_mizchi.data import (
        demo_graph,
        weighted_graph_50,
        weighted_graph_100,
        graph_to_txt,
        verify_result,
    )
    from bmssp_visualizer_mizchi.runner import run_batch, launch_traced_process


def safe_json_dumps(data: Any, indent: int | None = None) -> str:
    def sanitize(obj: Any) -> Any:
        if isinstance(obj, float):
            if math.isinf(obj) or math.isnan(obj):
                return None
        elif isinstance(obj, dict):
            return {k: sanitize(v) for k, v in obj.items()}
        elif isinstance(obj, (list, tuple)):
            return [sanitize(v) for v in obj]
        return obj

    raw = json.dumps(sanitize(data), ensure_ascii=False, indent=indent)
    return (
        raw.replace('&', '\\u0026')
        .replace('<', '\\u003c')
        .replace('>', '\\u003e')
        .replace('\u2028', '\\u2028')
        .replace('\u2029', '\\u2029')
    )


def build_offline_html(graph: Dict[str, Any], result: Dict[str, Any], events: List[Dict[str, Any]], comparison: Dict[str, Any]) -> str:
    payload = safe_json_dumps({
        'graph': graph,
        'result': result,
        'events': events,
        'algorithm': result.get('algorithm_id', 'dmm25'),
        'source': graph.get('source', 0),
        'comparison': comparison,
    })

    index_file = DIST_DIR / 'index.html'
    template = index_file.read_text(encoding='utf-8')
    css = (DIST_DIR / 'style.css').read_text(encoding='utf-8') if (DIST_DIR / 'style.css').exists() else ''
    js = (DIST_DIR / 'app.js').read_text(encoding='utf-8') if (DIST_DIR / 'app.js').exists() else ''

    template = re.sub(r'<link\b[^>]*href=["\'][^"\']*style\.css[^"\']*["\'][^>]*>', lambda _: f'<style>{css}</style>', template)
    template = re.sub(r'<script\b[^>]*src=["\'][^"\']*app\.js[^"\']*["\'][^>]*>\s*</script>', '', template)
    js_safe = js.replace('</script', '<\\/script')
    script = f'<script id="offline-data" type="application/json">{payload}</script>\n<script data-app="true">{js_safe}</script>'
    return template.replace('</body>', script + '\n</body>')


def collect_trace_events(input_path: Path, source: int, algorithm: str, sample_every: int = 1) -> tuple[List[Dict[str, Any]], Dict[str, Any]]:
    proc = launch_traced_process(input_path, source, algorithm, 'trace', sample_every)
    events = []
    native_result = None

    try:
        while True:
            line = proc.stdout.readline()
            if not line:
                break
            line = line.strip()
            if not line:
                continue
            ev = json.loads(line)
            if ev.get('type') == 'native_result':
                native_result = ev
                break
            events.append(ev)
            # Send ACK
            try:
                proc.stdin.write('next\n')
                proc.stdin.flush()
            except (BrokenPipeError, OSError):
                break
    finally:
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=2.0)

    if not native_result:
        raise RuntimeError(f"Runner {algorithm} không phát ra native_result.")
    return events, native_result


def main():
    print("[M5] Bắt đầu sinh dataset và dữ liệu mẫu cho BMSSP Mizchi Lab...")
    datasets = [
        ('demo', demo_graph()),
        ('weighted_50', weighted_graph_50()),
        ('weighted_100', weighted_graph_100()),
    ]

    algo_names = {
        'dmm25': 'DMM+25 (BMSSP Rust · N_reduced gadget)',
        'chd': 'C-HD (Hop-constrained BMSSP Rust)',
        'dijkstra': 'Dijkstra (Rust Binary Heap reference)',
    }

    for name, g in datasets:
        print(f"\n--- Xử lý dataset: {name} (n={g['n']}, m={len(g['edges'])}) ---")
        txt_path = DATA_DIR / f"{name}.txt"
        json_path = DATA_DIR / f"{name}.json"

        # Ghi file TXT và JSON vào data/
        txt_content = graph_to_txt(g)
        txt_path.write_text(txt_content, encoding='utf-8')
        json_path.write_text(json.dumps(g, ensure_ascii=False, indent=2), encoding='utf-8')
        print(f"  [+] Đã lưu {txt_path} ({txt_path.stat().st_size} bytes)")
        print(f"  [+] Đã lưu {json_path} ({json_path.stat().st_size} bytes)")

        # Chạy kiểm chứng và thu thập kết quả cho cả 3 thuật toán
        comparison = {}
        source = g.get('source', 0)

        for algo in ['dmm25', 'chd', 'dijkstra']:
            t0 = time.perf_counter()
            native = run_batch(txt_path, source, algo)
            wall_ms = (time.perf_counter() - t0) * 1000.0
            v_report = verify_result(g, source, native)

            if not v_report['ok']:
                raise RuntimeError(f"Lỗi: {algo} sai khoảng cách trên dataset {name}: {v_report}")

            res_obj = {
                'run_id': f"{name}_{algo}",
                'algorithm': algo_names[algo],
                'algorithm_id': algo,
                'source': source,
                'n': native['n'],
                'm': native.get('m', len(g['edges'])),
                'distances': native['distances'],
                'predecessors': native.get('predecessors'),
                'predecessor_edges': native.get('predecessor_edges'),
                'reachable': [x is not None for x in native['distances']],
                'compute_ms': native.get('compute_ms', 0.0),
                'wall_ms': wall_ms,
                'control_wait_ms': 0.0,
                'mode': 'fast',
                'stats': native.get('stats', {}),
                'verification': v_report,
                'verified': v_report['ok'],
                'provenance': {
                    'repo': 'https://github.com/mizchi/check-arxiv-2504-17033',
                    'commit': 'b3f173ca43d4052c0872d824be0bd61456ab7dd3',
                    'rustc': '1.100.0',
                    'algorithm': algo,
                },
            }
            comparison[algo] = res_obj
            print(f"  [✓] {algo:8s}: compute={native.get('compute_ms', 0.0):.4f}ms, wall={wall_ms:.2f}ms, Oracle PASS (cost đích={native['distances'][g.get('target', g['n']-1)]})")

        # Thu thập trace events của dmm25 để replay trong HTML offline
        print(f"  [*] Thu thập trace events cho DMM+25...")
        sample_every = 1 if g['n'] <= 50 else 2
        events, traced_native = collect_trace_events(txt_path, source, 'dmm25', sample_every)
        print(f"  [+] Đã thu thập {len(events)} trace events (sample_every={sample_every})")

        # Lưu trace.ndjson vào exports/
        trace_file = EXPORTS_DIR / f"{name}_trace.ndjson"
        with trace_file.open('w', encoding='utf-8') as f:
            for ev in events:
                f.write(json.dumps(ev, ensure_ascii=False) + '\n')

        # Lưu CSV khoảng cách
        csv_file = EXPORTS_DIR / f"{name}_distances.csv"
        with csv_file.open('w', encoding='utf-8', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(['vertex', 'distance', 'predecessor', 'reachable'])
            for v in range(g['n']):
                d = comparison['dmm25']['distances'][v]
                p = comparison['dmm25']['predecessors'][v] if comparison['dmm25'].get('predecessors') else ''
                writer.writerow([v, d if d is not None else '', p, 'true' if d is not None else 'false'])

        # Lưu result.json
        result_file = EXPORTS_DIR / f"{name}_result.json"
        result_file.write_text(json.dumps(comparison['dmm25'], ensure_ascii=False, indent=2), encoding='utf-8')

        # Tạo file HTML ngoại tuyến tự chứa
        html_file = EXPORTS_DIR / f"{name}.html"
        html_content = build_offline_html(g, comparison['dmm25'], events, comparison)
        html_file.write_text(html_content, encoding='utf-8')
        print(f"  [+] Đã xuất file HTML tự chứa: {html_file} ({html_file.stat().st_size:,} bytes)")

    # Đóng gói zip
    zip_path = EXPORTS_DIR / 'mizchi-demo-results.zip'
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        for f in EXPORTS_DIR.glob('*.*'):
            if f.suffix != '.zip':
                zf.write(f, arcname=f.name)
        for f in DATA_DIR.glob('*.*'):
            zf.write(f, arcname=f"data/{f.name}")
    print(f"\n[+] Đã đóng gói lưu trữ: {zip_path} ({zip_path.stat().st_size:,} bytes)")
    print("[M5] Hoàn thành sinh dataset và dữ liệu mẫu!")


if __name__ == '__main__':
    main()
