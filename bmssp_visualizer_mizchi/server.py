"""Backend HTTP server for BMSSP Mizchi Visualizer (port 8766).

Supports 3 algorithms: dmm25, chd, and dijkstra.
Integrates batch_runner, independent Python Dijkstra Oracle verification,
dataset catalog, and file exports.
"""
from __future__ import annotations

import argparse
import copy
import csv
import io
import hashlib
import json
import math
import mimetypes
import os
import signal
import socket
import subprocess
import sys
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit
from typing import Any, Dict, List, Optional

try:
    from data import (
        demo_graph,
        weighted_graph_50,
        weighted_graph_100,
        normalize_graph,
        parse_text,
        integer,
        verify_result,
    )
    from runner import run_batch, launch_traced_process, ALGORITHMS
except ImportError:
    from bmssp_visualizer_mizchi.data import (
        demo_graph,
        weighted_graph_50,
        weighted_graph_100,
        normalize_graph,
        parse_text,
        integer,
        verify_result,
    )
    from bmssp_visualizer_mizchi.runner import run_batch, launch_traced_process, ALGORITHMS

ROOT = Path(__file__).resolve().parent
DIST = ROOT / 'dist'
INPUTS = ROOT.parent / 'bmssp_comparison' / 'data'
RUNS = ROOT / 'runs'
TERMINAL = {'completed', 'error', 'stopped'}
MAX_EVENTS = 200000
RUNS.mkdir(parents=True, exist_ok=True)



def encode(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(',', ':'))


def safe_json_dumps(data: Any, indent: Optional[int] = None) -> str:
    """Serialize JSON safely for direct embedding inside <script> tags."""
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


class Run:
    def __init__(self, graph: Dict[str, Any], source: int, algorithm: str = 'dmm25', mode: str = 'controlled', delay: int = 100):
        self.id = uuid.uuid4().hex[:16]
        self.graph = copy.deepcopy(graph)
        self.graph['source'] = source
        self.source = source
        self.algorithm = algorithm
        self.mode = mode
        self.delay_ms = delay
        self.graph_hash = hashlib.sha256(
            encode({'n': graph['n'], 'edges': [[e['u'], e['v'], e['w']] for e in graph['edges']]}).encode('utf-8')
        ).hexdigest()
        self.created = time.perf_counter()
        self.status = 'pending'
        self.cv = threading.Condition()
        self.result: Optional[Dict[str, Any]] = None
        self.error: Optional[str] = None
        self.events: List[Dict[str, Any]] = []
        self.sampled = False
        self.dropped = 0
        self.step_budget = 0
        self.process: Optional[subprocess.Popen] = None
        self.finished = threading.Event()
        self.directory = RUNS / self.id
        self.directory.mkdir(parents=True, exist_ok=True)
        (self.directory / 'graph.json').write_text(encode(self.graph), encoding='utf-8')
        self.input_path = self.directory / 'input.txt'
        with self.input_path.open('w', encoding='utf-8') as f:
            f.write(f"{graph['n']} {len(graph['edges'])}\n")
            for e in graph['edges']:
                f.write(f"{e['u']} {e['v']} {e['w']:.17g}\n")

    def snapshot(self) -> Dict[str, Any]:
        with self.cv:
            return {
                'run_id': self.id,
                'status': self.status,
                'graph_id': self.graph.get('id', 'custom'),
                'source': self.source,
                'algorithm': self.algorithm,
                'mode': self.mode,
                'result': self.result,
                'events_count': len(self.events),
                'sampled': self.sampled,
                'dropped_events': self.dropped,
                'error': self.error,
                'elapsed_ms': (time.perf_counter() - self.created) * 1000.0,
            }

    def emit(self, event: Dict[str, Any], force: bool = False) -> None:
        with self.cv:
            if len(self.events) >= MAX_EVENTS and not force:
                self.sampled = True
                self.dropped += 1
                return
            item = {
                **event,
                'run_id': self.id,
                'seq': len(self.events) + 1,
                'elapsed_ms': (time.perf_counter() - self.created) * 1000.0,
            }
            self.events.append(item)
            with (self.directory / 'trace.ndjson').open('a', encoding='utf-8') as f:
                f.write(encode(item) + '\n')
            self.cv.notify_all()

    def control(self, action: str, delay: Optional[int] = None) -> Dict[str, Any]:
        if action not in ('pause', 'resume', 'step', 'stop', 'speed'):
            raise ValueError(f'Điều khiển không hợp lệ: {action}.')
        with self.cv:
            if delay is not None:
                self.delay_ms = integer(delay, 'Nhịp demo', 0, 1000)
            if self.status in TERMINAL:
                return self.snapshot()
            if action == 'stop':
                self.status = 'stopped'
                if self.process and self.process.poll() is None:
                    try:
                        self.process.terminate()
                    except OSError:
                        pass
                self.emit({'type': 'stopped'}, True)
            elif self.mode in ('batch', 'fast') and action != 'speed':
                raise ValueError('Chế độ chạy không trace chỉ hỗ trợ Dừng.')
            elif action == 'pause':
                self.status = 'paused'
                self.step_budget = 0
            elif action == 'resume':
                self.status = 'running'
                self.step_budget = 0
            elif action == 'step':
                self.status = 'paused'
                self.step_budget += 1
            self.emit({'type': 'status', 'status': self.status, 'delay_ms': self.delay_ms}, True)
            self.cv.notify_all()
            return self.snapshot()

    def allow_next(self) -> bool:
        with self.cv:
            while True:
                if self.status == 'stopped':
                    return False
                if self.status == 'paused':
                    if self.step_budget > 0:
                        self.step_budget -= 1
                        return True
                    self.cv.wait(0.25)
                    continue
                delay = self.delay_ms / 1000.0
                if delay:
                    changed = self.cv.wait(delay)
                    if changed:
                        continue
                return self.status != 'stopped'

    def work(self) -> None:
        started = time.perf_counter()
        with self.cv:
            if self.status == 'stopped':
                self.finished.set()
                return
            self.status = 'running'
            self.cv.notify_all()

        try:
            sample = 1 if self.graph['n'] <= 150 else max(1, math.ceil(len(self.graph['edges']) * 8 / 2500))
            self.sampled = self.mode == 'controlled' and sample > 1

            if self.mode in ('batch', 'fast'):
                self.emit({
                    'type': 'start',
                    'n': self.graph['n'],
                    'source': self.source,
                    'algorithm': self.algorithm,
                    'algorithm_id': self.algorithm,
                    'sampled': False,
                }, True)
                self.process = launch_traced_process(
                    self.input_path, self.source, self.algorithm, 'plain', 1
                )
                stdout_data, _ = self.process.communicate(timeout=20)
                if self.process.returncode != 0:
                    raise RuntimeError(f"Runner thất bại với mã {self.process.returncode}")
                native = json.loads(stdout_data.strip())
            else:
                self.process = launch_traced_process(
                    self.input_path, self.source, self.algorithm, 'trace', sample
                )
                native = None
                for line in self.process.stdout:
                    if self.status == 'stopped':
                        break
                    line = line.strip()
                    if not line:
                        continue
                    event = json.loads(line)
                    if event.get('type') == 'native_result':
                        native = event
                        break
                    self.emit(event)
                    if not self.allow_next():
                        break
                    try:
                        self.process.stdin.write('next\n')
                        self.process.stdin.flush()
                    except (BrokenPipeError, OSError):
                        break

            with self.cv:
                if self.status == 'stopped':
                    self.finished.set()
                    return

            if not native:
                raise RuntimeError('Runner đã kết thúc mà không phát ra native_result.')

            v_report = verify_result(self.graph, self.source, native)
            total_wall_ms = (time.perf_counter() - started) * 1000.0
            algo_names = {
                'dmm25': 'DMM+25 (BMSSP Rust · N_reduced gadget)',
                'chd': 'C-HD (Hop-constrained BMSSP Rust)',
                'dijkstra': 'Dijkstra (Rust Binary Heap reference)',
            }
            result_obj: Dict[str, Any] = {
                'run_id': self.id,
                'algorithm': algo_names.get(self.algorithm, self.algorithm),
                'algorithm_id': self.algorithm,
                'source': self.source,
                'n': native['n'],
                'm': native.get('m', len(self.graph['edges'])),
                'distances': native['distances'],
                'predecessors': native.get('predecessors'),
                'predecessor_edges': native.get('predecessor_edges'),
                'reachable': [x is not None for x in native['distances']],
                'compute_ms': native.get('compute_ms', 0.0),
                'wall_ms': total_wall_ms,
                'control_wait_ms': native.get('control_wait_ms', 0.0),
                'mode': self.mode,
                'trace': {
                    'sampled': self.sampled,
                    'events': len(self.events),
                    'sample_every': native.get('sample_every', 1),
                },
                'stats': native.get('stats', {}),
                'verification': v_report,
                'verified': v_report['ok'],
                'provenance': {
                    'repo': 'https://github.com/mizchi/check-arxiv-2504-17033',
                    'commit': 'b3f173ca43d4052c0872d824be0bd61456ab7dd3',
                    'adapter': 'src/bin/traced_runner.rs' if self.mode == 'controlled' else 'src/bin/batch_runner.rs',
                    'rustc': '1.100.0',
                    'algorithm': self.algorithm,
                    'execution_mode': self.mode,
                },
            }
            with self.cv:
                if self.status == 'stopped':
                    self.finished.set()
                    return
                self.result = result_obj
                self.status = 'completed'
                (self.directory / 'result.json').write_text(encode(result_obj), encoding='utf-8')
                self.emit({'type': 'result', 'result': result_obj}, True)
                self.cv.notify_all()
        except Exception as exc:
            with self.cv:
                if self.status != 'stopped':
                    self.error = str(exc)
                    self.status = 'error'
                    self.emit({'type': 'error', 'error': self.error}, True)
                    self.cv.notify_all()
        finally:
            if self.process and self.process.poll() is None:
                try:
                    self.process.terminate()
                except OSError:
                    pass
            self.finished.set()
            with self.cv:
                self.cv.notify_all()


class App:
    def __init__(self):
        self.graphs: Dict[str, Dict[str, Any]] = {
            'demo': demo_graph(),
            'weighted_50': weighted_graph_50(),
            'weighted_100': weighted_graph_100(),
        }
        self.runs: Dict[str, Run] = {}
        self.lock = threading.RLock()
        self.catalog = [
            {
                'id': 'demo',
                'name': self.graphs['demo']['name'],
                'n': self.graphs['demo']['n'],
                'm': len(self.graphs['demo']['edges']),
                'description': 'Đồ thị minh họa 18 đỉnh (có trọng số 0, cạnh song song, đỉnh 17 không tới được).',
            },
            {
                'id': 'weighted_50',
                'name': self.graphs['weighted_50']['name'],
                'n': 50,
                'm': len(self.graphs['weighted_50']['edges']),
                'description': 'Đồ thị 50 đỉnh có trọng số 1–15 dùng để quan sát biến đổi bậc và so sánh 3 thuật toán.',
            },
            {
                'id': 'weighted_100',
                'name': self.graphs['weighted_100']['name'],
                'n': 100,
                'm': len(self.graphs['weighted_100']['edges']),
                'description': 'Đồ thị 100 đỉnh có trọng số 1–20 dùng để đối chiếu 3 thuật toán.',
            },
        ]
        if INPUTS.exists():
            for path in sorted(INPUTS.glob('*.json')):
                stem = path.stem
                sizes = {
                    'counterexample_1500': (1500, 1501),
                    'real_2000': (2000, 7996),
                    'real_10000': (10000, 39996),
                    'real_50000': (50000, 199996),
                    'integer_10000': (10000, 39996),
                    'zero_ties_10000': (10000, 39996),
                    'USA-road-t.NY': (264346, 733846),
                    'USA-road-t.BAY': (321270, 800172),
                    'USA-road-t.COL': (435666, 1057066),
                }
                if stem in sizes:
                    n, m = sizes[stem]
                    desc = ('Mạng lưới đường bộ thực tế (DIMACS-9) dùng trong benchmark.'
                            if 'USA-road' in stem else
                            'Bộ dữ liệu benchmark BMSSP kế thừa.')
                    self.catalog.append({
                        'id': stem,
                        'name': stem,
                        'n': n,
                        'm': m,
                        'description': desc,
                    })

    def graph(self, gid: str) -> Dict[str, Any]:
        with self.lock:
            if gid not in self.graphs:
                if gid not in {x['id'] for x in self.catalog}:
                    raise KeyError(f'Không tìm thấy đồ thị: {gid}')
                raw = json.loads((INPUTS / f'{gid}.json').read_text(encoding='utf-8'))
                raw['id'] = gid
                self.graphs[gid] = normalize_graph(raw, gid)
            return self.graphs[gid]

    def upload(self, payload: Dict[str, Any]) -> Dict[str, Any]:
        if 'graph' in payload:
            graph = normalize_graph(payload['graph'], payload.get('name'))
        else:
            graph = parse_text(payload.get('text', ''), payload.get('format', 'txt'), payload.get('name'))
        graph['id'] = f'upload_{uuid.uuid4().hex[:12]}'
        with self.lock:
            self.graphs[graph['id']] = graph
            self.catalog.append({
                'id': graph['id'],
                'name': graph['name'],
                'n': graph['n'],
                'm': len(graph['edges']),
                'description': 'Đồ thị do người dùng tải lên.',
            })
        return graph

    def start(self, payload: Dict[str, Any]) -> Run:
        graph_id = payload.get('graph_id', 'demo')
        graph = self.graph(graph_id)
        source = integer(payload.get('source', graph['source']), 'Đỉnh nguồn', 0, graph['n'] - 1)
        algorithm = payload.get('algorithm', 'dmm25')
        if algorithm not in ALGORITHMS:
            raise ValueError(f'Thuật toán không hợp lệ: {algorithm}. Phải là {sorted(ALGORITHMS)}.')
        mode = payload.get('mode', 'batch')
        delay = integer(payload.get('delay', 100), 'Nhịp demo', 0, 1000)
        with self.lock:
            if any(r.status == 'running' for r in self.runs.values()):
                raise BlockingIOError('Một lượt chạy đang thực thi. Hãy chờ hoặc Dừng trước khi chạy tiếp.')
            run = Run(graph, source, algorithm, mode, delay)
            self.runs[run.id] = run
            threading.Thread(target=run.work, daemon=True, name=f'mizchi-{run.id}').start()
            return run

    def get_run(self, rid: str) -> Run:
        with self.lock:
            if rid not in self.runs:
                raise KeyError(f'Không tìm thấy lần chạy: {rid}')
            return self.runs[rid]

    def list_runs(self, graph_id: Optional[str] = None) -> List[Dict[str, Any]]:
        with self.lock:
            results = []
            for r in self.runs.values():
                snap = r.snapshot()
                if graph_id is None or snap.get('graph_id') == graph_id:
                    results.append(snap)
            return results

    def close(self) -> None:
        with self.lock:
            runs = list(self.runs.values())
        for r in runs:
            if r.status not in TERMINAL:
                r.control('stop')
        for r in runs:
            r.finished.wait(3.0)

def export_csv(run: Run) -> str:
    text = io.StringIO(newline='')
    writer = csv.writer(text)
    writer.writerow(['vertex', 'distance', 'reachable'])
    r = run.result or {}
    distances = r.get('distances', [])
    for v in range(run.graph['n']):
        d = distances[v] if v < len(distances) else None
        writer.writerow([v, d if d is not None else '', 'true' if d is not None else 'false'])
    return text.getvalue()


def export_html(run: Run) -> str:
    payload = safe_json_dumps({
        'graph': run.graph,
        'result': run.result,
        'events': run.events,
        'algorithm': run.algorithm,
        'source': run.source,
    })
    index_file = DIST / 'index.html'
    if index_file.exists():
        template = index_file.read_text(encoding='utf-8')
        css = (DIST / 'style.css').read_text(encoding='utf-8') if (DIST / 'style.css').exists() else ''
        js = (DIST / 'app.js').read_text(encoding='utf-8') if (DIST / 'app.js').exists() else ''
        import re
        template = re.sub(r'<link\b[^>]*href=["\'][^"\']*style\.css[^"\']*["\'][^>]*>', lambda _: f'<style>{css}</style>', template)
        template = re.sub(r'<script\b[^>]*src=["\'][^"\']*app\.js[^"\']*["\'][^>]*>\s*</script>', '', template)
        js_safe = js.replace('</script', '<\\/script')
        script = f'<script id="offline-data" type="application/json">{payload}</script>\n<script data-app="true">{js_safe}</script>'
        return template.replace('</body>', script + '\n</body>')

    res = run.result or {}
    stats = res.get('stats', {})
    dist = res.get('distances', [])
    v_report = res.get('verification', {})
    is_ok = bool(v_report.get('ok'))
    badge_cls = 'badge success' if is_ok else 'badge'
    badge_text = 'HỢP LỆ (PASS)' if is_ok else 'LỖI'
    first_rows = ''.join(
        f"<tr><td>{v}</td><td>{dist[v] if v < len(dist) and dist[v] is not None else '∞'}</td><td>{'Có' if v < len(dist) and dist[v] is not None else 'Không'}</td></tr>"
        for v in range(min(10, run.graph.get('n', 0)))
    )

    return f"""<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>BMSSP Mizchi Demo - {run.graph.get('name', 'Đồ thị')} ({run.algorithm})</title>
<style>
  body {{ font-family: system-ui, sans-serif; background: #0f172a; color: #f8fafc; padding: 24px; line-height: 1.5; }}
  .card {{ background: #1e293b; border-radius: 8px; padding: 16px; margin-bottom: 16px; border: 1px solid #334155; }}
  table {{ width: 100%; border-collapse: collapse; }}
  th, td {{ padding: 8px 12px; text-align: left; border-bottom: 1px solid #334155; }}
  .badge {{ display: inline-block; padding: 2px 8px; border-radius: 4px; font-weight: bold; background: #0284c7; color: white; }}
  .badge.success {{ background: #16a34a; }}
</style>
</head>
<body>
<h1>BMSSP Visualizer (Mizchi Rust Engine) — Kết quả chạy offline</h1>
<div class="card">
  <h2>Thông tin lần chạy</h2>
  <p><strong>Run ID:</strong> {run.id}</p>
  <p><strong>Thuật toán:</strong> <span class="badge">{res.get('algorithm', run.algorithm)}</span></p>
  <p><strong>Đỉnh nguồn:</strong> {run.source} | <strong>Số đỉnh:</strong> {run.graph.get('n')} | <strong>Số cạnh:</strong> {len(run.graph.get('edges', []))}</p>
  <p><strong>Thời gian tính toán lõi:</strong> {res.get('compute_ms', 0.0):.3f} ms (Tổng wall: {res.get('wall_ms', 0.0):.3f} ms)</p>
  <p><strong>Đối chiếu Dijkstra Oracle:</strong> <span class="{badge_cls}">{badge_text}</span></p>
</div>
<div class="card">
  <h2>Tham số & Thống kê thuật toán</h2>
  <table>
    <tr><th>Chỉ số</th><th>Giá trị</th></tr>
    <tr><td>Số đỉnh sau rút gọn (N')</td><td>{stats.get('n_transformed', 'N/A')}</td></tr>
    <tr><td>Số cạnh sau rút gọn (M')</td><td>{stats.get('m_transformed', 'N/A')}</td></tr>
    <tr><td>Tham số k (pivots scale)</td><td>{stats.get('k', 'N/A')}</td></tr>
    <tr><td>Tham số t (hop bound)</td><td>{stats.get('t', 'N/A')}</td></tr>
    <tr><td>Thời gian tiền xử lý (preprocess_ms)</td><td>{stats.get('preprocess_ms', 'N/A')}</td></tr>
    <tr><td>Thời gian lõi (core_ms)</td><td>{stats.get('core_ms', 'N/A')}</td></tr>
  </table>
</div>
<div class="card">
  <h2>Bảng khoảng cách (10 đỉnh đầu)</h2>
  <table>
    <tr><th>Đỉnh</th><th>Khoảng cách</th><th>Tới được?</th></tr>
    {first_rows}
  </table>
</div>
<script id="offline-data" type="application/json">{payload}</script>
</body>
</html>"""

class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def handle(self):
        try:
            super().handle()
        except (ConnectionResetError, ConnectionAbortedError, BrokenPipeError):
            pass

    def log_message(self, fmt, *args):
        if getattr(self.server, 'verbose', False):
            super().log_message(fmt, *args)

    @property
    def app(self) -> App:
        return self.server.app

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

    def respond(self, value: Any, status: int = 200, content_type: str = 'application/json; charset=utf-8', filename: Optional[str] = None):
        raw = value if isinstance(value, bytes) else (value if isinstance(value, str) else encode(value)).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(raw)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Access-Control-Allow-Origin', '*')
        if filename:
            self.send_header('Content-Disposition', f'attachment; filename="{filename}"')
        self.end_headers()
        self.wfile.write(raw)

    def payload(self) -> Dict[str, Any]:
        size = int(self.headers.get('Content-Length', '0'))
        if size < 1 or size > 40 * 1024 * 1024:
            raise ValueError('Dữ liệu yêu cầu trống hoặc vượt 40 MB.')
        obj = json.loads(self.rfile.read(size).decode('utf-8'))
        if not isinstance(obj, dict):
            raise ValueError('Yêu cầu JSON phải là một object.')
        return obj

    def do_GET(self):
        self.dispatch(False)

    def do_POST(self):
        self.dispatch(True)

    def dispatch(self, post: bool):
        try:
            parsed = urlsplit(self.path)
            path = parsed.path
            query = parse_qs(parsed.query)

            if path == '/api/datasets' and not post:
                return self.respond({'datasets': self.app.catalog})

            if path == '/api/graph' and not post:
                gid = query.get('id', ['demo'])[0]
                return self.respond(self.app.graph(gid))

            if path == '/api/graphs' and post:
                graph = self.app.upload(self.payload())
                return self.respond({'id': graph['id'], 'graph': graph})

            if path == '/api/runs' and post:
                run = self.app.start(self.payload())
                return self.respond({'run_id': run.id, 'status': run.status, 'algorithm': run.algorithm})

            if path == '/api/runs' and not post:
                gid = query.get('graph_id', [None])[0]
                return self.respond({'runs': self.app.list_runs(gid)})

            if path.startswith('/api/runs/'):
                parts = path.strip('/').split('/')
                run_id = parts[2]
                run = self.app.get_run(run_id)
                if len(parts) == 3 and not post:
                    if query.get('wait', ['false'])[0].lower() in ('1', 'true', 'yes'):
                        run.finished.wait(timeout=30.0)
                    return self.respond(run.snapshot())

                action = parts[3] if len(parts) == 4 else ''
                if action == 'events' and not post:
                    after = int(query.get('after', [0])[0])
                    return self.stream(run, after)

                if action == 'control' and post:
                    payload = self.payload()
                    return self.respond(run.control(payload.get('action', 'stop'), payload.get('delay')))

                if action == 'export' and not post:
                    kind = query.get('kind', ['result'])[0]
                    return self.download(run, kind)

            if not post and path in ('/', '/index.html', '/app.js', '/style.css'):
                file = DIST / ('index.html' if path == '/' else path[1:])
                if not file.exists():
                    return self.respond({
                        'status': 'frontend_not_built',
                        'message': 'Giao diện web đang phát triển trong M4. Sử dụng REST API trên port 8766.',
                    }, status=503)
                content_type, _ = mimetypes.guess_type(file.name)
                return self.respond(file.read_bytes(), content_type=(content_type or 'text/plain') + '; charset=utf-8')

            return self.respond({'error': 'Không tìm thấy tài nguyên.'}, 404)

        except BlockingIOError as exc:
            self.respond({'error': str(exc)}, 409)
        except KeyError as exc:
            self.respond({'error': str(exc)}, 404)
        except (ValueError, TypeError, OverflowError) as exc:
            self.respond({'error': str(exc)}, 400)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass
        except Exception as exc:
            self.respond({'error': f'Lỗi máy chủ nội bộ: {exc}'}, 500)

    def stream(self, run: Run, after: int):
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream; charset=utf-8')
        self.send_header('Cache-Control', 'no-cache')
        self.send_header('Connection', 'close')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.close_connection = True
        cursor = max(0, after)
        while True:
            with run.cv:
                pending = [e for e in run.events if e['seq'] > cursor]
                done = run.status in TERMINAL
                if not pending and not done:
                    run.cv.wait(1.0)
                    pending = [e for e in run.events if e['seq'] > cursor]
                    done = run.status in TERMINAL
            if pending:
                for event in pending:
                    self.wfile.write(('data: ' + encode(event) + '\n\n').encode('utf-8'))
                    cursor = event['seq']
            elif done:
                break
            else:
                self.wfile.write(b': heartbeat\n\n')
            self.wfile.flush()

    def download(self, run: Run, kind: str):
        if kind == 'graph':
            return self.respond(run.graph, filename='graph.json')
        if kind == 'trace':
            return self.respond(
                ''.join(encode(e) + '\n' for e in run.events),
                content_type='application/x-ndjson; charset=utf-8',
                filename='trace.ndjson',
            )
        if run.result is None and run.status not in TERMINAL:
            raise ValueError('Lần chạy chưa hoàn tất. Chưa có file kết quả để tải.')
        if kind == 'result':
            if run.result is None:
                raise ValueError('Lần chạy không có kết quả hợp lệ.')
            return self.respond(run.result, filename='result.json')
        if kind == 'csv':
            return self.respond(export_csv(run), content_type='text/csv; charset=utf-8', filename='distances.csv')
        if kind == 'html':
            return self.respond(export_html(run), content_type='text/html; charset=utf-8', filename='demo.html')
        raise ValueError(f'Loại file xuất không hợp lệ: {kind}. Chỉ hỗ trợ result, graph, csv, html, trace.')



class LocalServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def server_close(self):
        if hasattr(self, 'app'):
            self.app.close()
        super().server_close()


def create_server(host: str = '127.0.0.1', port: int = 8766, verbose: bool = False) -> LocalServer:
    server = LocalServer((host, port), Handler)
    server.app = App()
    server.verbose = verbose
    return server


def main():
    parser = argparse.ArgumentParser(description='BMSSP Mizchi Local Visualizer Server')
    parser.add_argument('--host', type=str, default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8766)
    parser.add_argument('--verbose', action='store_true')
    args = parser.parse_args()

    server = None
    try:
        server = create_server(host=args.host, port=args.port, verbose=args.verbose)
        actual_port = server.server_address[1]
        print('=' * 65)
        print(f'BMSSP Mizchi Visualizer Server đang chạy tại: http://{args.host}:{actual_port}')
        print('Hỗ trợ 3 thuật toán: DMM+25, C-HD, Dijkstra (Rust engine commit b3f173ca)')
        print('API Endpoints:')
        print('  - GET  /api/datasets')
        print('  - GET  /api/graph?id=<id>')
        print('  - POST /api/graphs')
        print('  - POST /api/runs')
        print('  - GET  /api/runs/<id>')
        print('  - GET  /api/runs/<id>/export?kind=result|graph|csv|html')
        print('=' * 65, flush=True)

        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.server_close()
    except OSError as err:
        if getattr(err, 'winerror', None) == 10048 or err.errno in (98, 48):
            print(f'\n[!] LỖI: Port {args.port} đã bị chiếm dụng bởi tiến trình khác.', file=sys.stderr)
            print(f'    Gợi ý giải phóng port: Get-Process hoặc netstat -ano | findstr :{args.port}', file=sys.stderr)
        else:
            print(f'[!] Không thể khởi động server: {err}', file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print('\nNhận tín hiệu dừng từ bàn phím. Đang thoát...', flush=True)
    finally:
        if server:
            server.server_close()


if __name__ == '__main__':
    main()

