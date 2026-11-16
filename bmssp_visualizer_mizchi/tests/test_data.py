"""Unit test suite for data validation, generators and Dijkstra Oracle."""
from __future__ import annotations

import json
import unittest
from pathlib import Path

from bmssp_visualizer_mizchi.data import (
    demo_graph,
    weighted_graph_50,
    weighted_graph_100,
    graph_to_txt,
    parse_text,
    normalize_graph,
    dijkstra,
    verify_result,
)


class TestDataModule(unittest.TestCase):
    def test_01_demo_graph(self):
        g = demo_graph()
        self.assertEqual(g['n'], 18)
        self.assertEqual(len(g['edges']), 30)
        self.assertEqual(g['source'], 0)
        self.assertEqual(g['target'], 16)
        # Đỉnh 17 không có cạnh tới
        dist, pred, pe = dijkstra(g, 0)
        self.assertEqual(dist[0], 0.0)
        self.assertEqual(dist[16], 11.0)
        self.assertIsNone(dist[17], "Đỉnh 17 phải unreachable từ nguồn 0.")

    def test_02_weighted_50_reachability(self):
        g = weighted_graph_50()
        self.assertEqual(g['n'], 50)
        self.assertEqual(len(g['edges']), 120)
        dist, pred, pe = dijkstra(g, 0)
        # Tất cả 50 đỉnh phải reachable từ 0
        for v in range(50):
            self.assertIsNotNone(dist[v], f"Đỉnh {v} trong weighted_50 phải reachable từ nguồn 0.")
        self.assertEqual(dist[49], 53.0)

    def test_03_weighted_100_reachability(self):
        g = weighted_graph_100()
        self.assertEqual(g['n'], 100)
        self.assertEqual(len(g['edges']), 240)
        dist, pred, pe = dijkstra(g, 0)
        for v in range(100):
            self.assertIsNotNone(dist[v], f"Đỉnh {v} trong weighted_100 phải reachable từ nguồn 0.")
        self.assertEqual(dist[99], 93.0)

    def test_04_graph_to_txt_roundtrip(self):
        g = demo_graph()
        txt = graph_to_txt(g)
        parsed = parse_text(txt, fmt='txt')
        self.assertEqual(parsed['n'], g['n'])
        self.assertEqual(len(parsed['edges']), len(g['edges']))
        for e_orig, e_new in zip(g['edges'], parsed['edges']):
            self.assertEqual(e_orig['u'], e_new['u'])
            self.assertEqual(e_orig['v'], e_new['v'])
            self.assertEqual(e_orig['w'], e_new['w'])

    def test_05_normalize_graph_validation(self):
        # n ngoài giới hạn
        with self.assertRaises(ValueError):
            normalize_graph({'n': 0, 'edges': []})
        with self.assertRaises(ValueError):
            normalize_graph({'n': 600_000, 'edges': []})
        # Đỉnh cạnh không hợp lệ
        with self.assertRaises(ValueError):
            normalize_graph({'n': 3, 'edges': [[0, 5, 1.0]]})
        # Trọng số âm
        with self.assertRaises(ValueError):
            normalize_graph({'n': 3, 'edges': [[0, 1, -1.0]]})
        # Trọng số vô hạn
        with self.assertRaises(ValueError):
            normalize_graph({'n': 3, 'edges': [[0, 1, float('inf')]]})
        # ID cạnh bị trùng
        with self.assertRaises(ValueError):
            normalize_graph({'n': 3, 'edges': [{'id': 1, 'u': 0, 'v': 1, 'w': 1.0}, {'id': 1, 'u': 1, 'v': 2, 'w': 2.0}]})

    def test_06_verify_result_modes(self):
        g = demo_graph()
        dist, _, _ = dijkstra(g, 0)
        # Kết quả chuẩn
        good_result = {'distances': dist}
        v = verify_result(g, 0, good_result, comparison_mode='auto')
        self.assertTrue(v['ok'])
        self.assertEqual(v['wrong_vertices'], 0)

        # Sai một đỉnh
        bad_dist = list(dist)
        bad_dist[1] = (bad_dist[1] or 0) + 1.0
        bad_result = {'distances': bad_dist}
        v_bad = verify_result(g, 0, bad_result, comparison_mode='auto')
        self.assertFalse(v_bad['ok'])
        self.assertIn(1, v_bad['first_wrong_vertices'])


if __name__ == '__main__':
    unittest.main()
