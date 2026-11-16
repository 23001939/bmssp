"""Unit tests for M3: Rust traced runner, live SSE streaming and interactive control."""
from __future__ import annotations

import json
import socket
import time
import unittest
import urllib.request
from pathlib import Path

from bmssp_visualizer_mizchi.build_rust import ensure_traced_built
from bmssp_visualizer_mizchi.runner import launch_traced_process
from bmssp_visualizer_mizchi.server import create_server


def find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


class TestTracedRunner(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        port = find_free_port()
        cls.server = create_server(host='127.0.0.1', port=port, verbose=False)
        cls.port = port
        cls.base = f'http://127.0.0.1:{port}'
        import threading
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        time.sleep(0.1)

    @classmethod
    def tearDownClass(cls):
        if cls.server:
            cls.server.shutdown()
            cls.server.server_close()

    def http_request(self, method: str, path: str, payload: dict | None = None, headers: dict | None = None):
        url = f'{self.base}{path}'
        data = json.dumps(payload).encode('utf-8') if payload is not None else None
        req_headers = {'Content-Type': 'application/json'}
        if headers:
            req_headers.update(headers)
        req = urllib.request.Request(url, data=data, headers=req_headers, method=method)
        try:
            with urllib.request.urlopen(req, timeout=10) as resp:
                status = resp.status
                body = resp.read()
                ct = resp.headers.get('Content-Type', '')
                if 'application/json' in ct:
                    return status, json.loads(body.decode('utf-8')), resp.headers
                return status, body.decode('utf-8', errors='replace'), resp.headers
        except urllib.error.HTTPError as err:
            body = err.read()
            try:
                parsed = json.loads(body.decode('utf-8'))
            except Exception:
                parsed = body.decode('utf-8', errors='replace')
            return err.code, parsed, err.headers

    def test_01_traced_binary_built(self):
        exe = ensure_traced_built()
        self.assertTrue(exe.is_file())
        self.assertEqual(exe.name.lower(), 'traced_runner.exe')

    def test_02_direct_process_stepping(self):
        tmp_dir = Path(__file__).resolve().parent.parent / 'runs' / 'tmp_test'
        tmp_dir.mkdir(parents=True, exist_ok=True)
        input_txt = tmp_dir / 'input.txt'
        input_txt.write_text("3 3\n0 1 2.0\n1 2 3.0\n0 2 10.0\n", encoding='utf-8')

        for algo in ('dijkstra', 'dmm25', 'chd'):
            proc = launch_traced_process(input_txt, 0, algo, mode='trace', sample_every=1)
            first_line = proc.stdout.readline()
            start_event = json.loads(first_line)
            self.assertEqual(start_event['type'], 'start')
            self.assertEqual(start_event['n'], 3)
            self.assertEqual(start_event['algorithm_id'], algo)

            native_res = None
            for _ in range(150):
                proc.stdin.write('next\n')
                proc.stdin.flush()
                line = proc.stdout.readline()
                if not line:
                    break
                ev = json.loads(line)
                if ev.get('type') == 'native_result':
                    native_res = ev
                    break

            proc.stdin.close()
            proc.stdout.close()
            proc.terminate()
            proc.wait(timeout=2)
            self.assertIsNotNone(native_res, f"Algorithm {algo} should emit native_result")
            self.assertEqual(native_res['distances'], [0.0, 2.0, 5.0])

    def test_03_controlled_run_with_sse_stream(self):
        status, start_data, _ = self.http_request(
            'POST', '/api/runs',
            {'graph_id': 'demo', 'source': 0, 'algorithm': 'dmm25', 'mode': 'controlled', 'delay': 1}
        )
        self.assertEqual(status, 200)
        run_id = start_data['run_id']

        url = f'{self.base}/api/runs/{run_id}/events'
        req = urllib.request.Request(url, headers={'Accept': 'text/event-stream'})
        with urllib.request.urlopen(req, timeout=10) as resp:
            self.assertTrue(resp.headers.get('Content-Type', '').startswith('text/event-stream'))
            events = []
            for raw_line in resp:
                line = raw_line.decode('utf-8').strip()
                if line.startswith('data: '):
                    ev = json.loads(line[6:])
                    events.append(ev)
                    if ev.get('type') == 'result':
                        break

        self.assertTrue(len(events) >= 5, "Controlled run must emit trace events")
        types = {e['type'] for e in events}
        self.assertIn('start', types)
        self.assertIn('relax', types)
        self.assertIn('result', types)

        status, trace_data, _ = self.http_request('GET', f'/api/runs/{run_id}/export?kind=trace')
        self.assertEqual(status, 200)
        self.assertTrue(len(trace_data.splitlines()) >= len(events) - 1)

    def test_04_control_pause_step_resume_stop(self):
        status, start_data, _ = self.http_request(
            'POST', '/api/runs',
            {'graph_id': 'weighted_100', 'source': 0, 'algorithm': 'chd', 'mode': 'controlled', 'delay': 50}
        )
        self.assertEqual(status, 200)
        run_id = start_data['run_id']

        status, snap, _ = self.http_request('POST', f'/api/runs/{run_id}/control', {'action': 'pause'})
        self.assertEqual(status, 200)
        self.assertEqual(snap['status'], 'paused')

        status, snap, _ = self.http_request('POST', f'/api/runs/{run_id}/control', {'action': 'step'})
        self.assertEqual(status, 200)

        status, snap, _ = self.http_request('POST', f'/api/runs/{run_id}/control', {'action': 'speed', 'delay': 5})
        self.assertEqual(status, 200)

        status, snap, _ = self.http_request('POST', f'/api/runs/{run_id}/control', {'action': 'resume'})
        self.assertEqual(status, 200)

        status, snap, _ = self.http_request('POST', f'/api/runs/{run_id}/control', {'action': 'stop'})
        self.assertEqual(status, 200)
        self.assertEqual(snap['status'], 'stopped')


if __name__ == '__main__':
    unittest.main()

