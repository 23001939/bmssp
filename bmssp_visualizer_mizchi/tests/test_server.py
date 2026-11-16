"""Integration test suite for BMSSP Mizchi Backend Server (M2)."""
from __future__ import annotations

import json
import socket
import threading
import time
import unittest
import urllib.error
import urllib.request
from typing import Any, Dict, Optional, Tuple

from bmssp_visualizer_mizchi.server import create_server


def find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


class TestMizchiServer(unittest.TestCase):
    server = None
    server_thread = None
    base_url = ''

    @classmethod
    def setUpClass(cls):
        port = find_free_port()
        cls.server = create_server(host='127.0.0.1', port=port, verbose=False)
        cls.base_url = f'http://127.0.0.1:{port}'
        cls.server_thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.server_thread.start()
        time.sleep(0.1)

    @classmethod
    def tearDownClass(cls):
        if cls.server:
            cls.server.shutdown()
            cls.server.server_close()

    def http_request(
        self, method: str, path: str, payload: Optional[Dict[str, Any]] = None
    ) -> Tuple[int, Dict[str, Any], bytes]:
        url = f'{self.base_url}{path}'
        data = json.dumps(payload).encode('utf-8') if payload is not None else None
        headers = {'Content-Type': 'application/json'} if data else {}
        req = urllib.request.Request(url, data=data, headers=headers, method=method)
        try:
            with urllib.request.urlopen(req, timeout=10) as resp:
                body = resp.read()
                try:
                    parsed = json.loads(body.decode('utf-8'))
                except Exception:
                    parsed = {}
                return resp.status, parsed, body
        except urllib.error.HTTPError as err:
            err_body = err.read()
            try:
                parsed = json.loads(err_body.decode('utf-8'))
            except Exception:
                parsed = {}
            return err.code, parsed, err_body

    def test_01_datasets_catalog(self):
        status, data, _ = self.http_request('GET', '/api/datasets')
        self.assertEqual(status, 200)
        self.assertIn('datasets', data)
        catalog_ids = [d['id'] for d in data['datasets']]
        self.assertIn('demo', catalog_ids)
        self.assertIn('weighted_100', catalog_ids)

    def test_02_get_graph(self):
        status, data, _ = self.http_request('GET', '/api/graph?id=demo')
        self.assertEqual(status, 200)
        self.assertEqual(data.get('n'), 18)
        self.assertEqual(len(data.get('edges', [])), 30)

    def test_03_upload_graph_txt(self):
        payload = {'text': "4 3\n0 1 2.5\n1 2 1.5\n2 3 3.0", 'format': 'txt', 'name': 'DAG'}
        status, data, _ = self.http_request('POST', '/api/graphs', payload)
        self.assertEqual(status, 200)
        self.assertTrue(data.get('id', '').startswith('upload_'))
        self.assertEqual(data.get('graph', {}).get('n'), 4)

    def test_04_runs_all_three_algorithms(self):
        for algo in ('dmm25', 'chd', 'dijkstra'):
            with self.subTest(algorithm=algo):
                status, start_data, _ = self.http_request('POST', '/api/runs', {'graph_id': 'demo', 'source': 0, 'algorithm': algo})
                self.assertEqual(status, 200)
                run_id = start_data['run_id']
                status, run_data, _ = self.http_request('GET', f'/api/runs/{run_id}?wait=true')
                self.assertEqual(status, 200)
                self.assertEqual(run_data['status'], 'completed')
                result = run_data['result']
                self.assertTrue(result['verified'], f"Algorithm {algo} failed Dijkstra verification")
                self.assertEqual(len(result['distances']), 18)
                self.assertIsNone(result['distances'][17])
                self.assertAlmostEqual(result['distances'][16], 11.0, places=5)

    def test_05_exports(self):
        _, start_data, _ = self.http_request('POST', '/api/runs', {'graph_id': 'demo', 'source': 0, 'algorithm': 'dmm25'})
        run_id = start_data['run_id']
        self.http_request('GET', f'/api/runs/{run_id}?wait=true')

        status, res_json, _ = self.http_request('GET', f'/api/runs/{run_id}/export?kind=result')
        self.assertEqual(status, 200)
        self.assertIn('distances', res_json)
        self.assertIn('stats', res_json)

        status, _, raw_csv = self.http_request('GET', f'/api/runs/{run_id}/export?kind=csv')
        self.assertEqual(status, 200)
        self.assertTrue(raw_csv.decode('utf-8').startswith('vertex,distance,reachable'))

        status, graph_json, _ = self.http_request('GET', f'/api/runs/{run_id}/export?kind=graph')
        self.assertEqual(status, 200)
        self.assertEqual(graph_json.get('n'), 18)

        status, _, raw_html = self.http_request('GET', f'/api/runs/{run_id}/export?kind=html')
        self.assertEqual(status, 200)
        self.assertIn('<!DOCTYPE html>', raw_html.decode('utf-8'))

    def test_06_error_cases(self):
        status, _, _ = self.http_request('GET', '/api/graph?id=does_not_exist_xyz')
        self.assertEqual(status, 404)

        status, _, _ = self.http_request('POST', '/api/runs', {'graph_id': 'demo', 'algorithm': 'invalid_algo'})
        self.assertEqual(status, 400)

        status, _, _ = self.http_request('GET', '/api/runs/non_existent_12345')
        self.assertEqual(status, 404)


if __name__ == '__main__':
    unittest.main(verbosity=2)
