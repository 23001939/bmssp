"""Differential fuzz testing: 3 Rust algorithms vs Python Dijkstra Oracle on 25 random graphs."""
from __future__ import annotations

import math
import random
import tempfile
import unittest
from pathlib import Path
from typing import Any, Dict, List

from bmssp_visualizer_mizchi.data import (
    normalize_graph,
    graph_to_txt,
    dijkstra,
    verify_result,
)
from bmssp_visualizer_mizchi.runner import run_batch


def generate_test_graph(
    n: int,
    kind: str,
    weight_type: str = 'int',
    seed: int = 42,
) -> Dict[str, Any]:
    rng = random.Random(seed)
    edges = []
    pairs = set()

    def rand_weight() -> float:
        if weight_type == 'zero_ties':
            return float(rng.choice([0.0, 1.0, 2.0]))
        elif weight_type == 'float':
            return round(rng.uniform(0.1, 20.0), 4)
        else:
            return float(rng.randint(0, 15))

    def add_edge(u: int, v: int, w: float | None = None) -> None:
        if u != v:
            edges.append([u, v, rand_weight() if w is None else w])

    if kind == 'chain':
        for i in range(n - 1):
            add_edge(i, i + 1)
        # Thêm vài cạnh tắt
        for _ in range(min(n, 20)):
            u = rng.randint(0, n - 2)
            v = rng.randint(u + 1, n - 1)
            add_edge(u, v)

    elif kind == 'star':
        for i in range(1, n):
            if rng.random() < 0.6:
                add_edge(0, i)
            if rng.random() < 0.4:
                add_edge(i, 0)
            if i < n - 1 and rng.random() < 0.3:
                add_edge(i, i + 1)

    elif kind == 'cycle':
        for i in range(n):
            add_edge(i, (i + 1) % n)
        # Random chords
        for _ in range(n):
            u, v = rng.randint(0, n - 1), rng.randint(0, n - 1)
            add_edge(u, v)

    elif kind == 'dense':
        m_target = min(n * (n - 1) // 3, 1000)
        while len(edges) < m_target:
            u, v = rng.randint(0, n - 1), rng.randint(0, n - 1)
            if u != v and (u, v) not in pairs:
                pairs.add((u, v))
                add_edge(u, v)

    else:  # sparse default
        # Spanning tree để bảo đảm kết nối một phần
        for i in range(1, n):
            parent = rng.randint(0, i - 1)
            add_edge(parent, i)
        # Cạnh ngẫu nhiên
        extra = min(n * 2, 500)
        for _ in range(extra):
            u, v = rng.randint(0, n - 1), rng.randint(0, n - 1)
            add_edge(u, v)

    source = 0
    return normalize_graph({
        'id': f"test_{kind}_{n}_{seed}",
        'name': f"Random {kind} n={n}",
        'n': n,
        'source': source,
        'edges': edges,
    })


class TestRandomDifferential(unittest.TestCase):
    def test_25_random_graphs_differential(self):
        configs = [
            (8, 'chain', 'int', 101),
            (12, 'star', 'zero_ties', 102),
            (15, 'cycle', 'float', 103),
            (20, 'sparse', 'int', 104),
            (25, 'dense', 'zero_ties', 105),
            (30, 'chain', 'float', 106),
            (35, 'star', 'int', 107),
            (40, 'cycle', 'zero_ties', 108),
            (45, 'sparse', 'float', 109),
            (50, 'dense', 'int', 110),
            (20, 'sparse', 'zero_ties', 111),
            (28, 'star', 'float', 112),
            (32, 'cycle', 'int', 113),
            (60, 'chain', 'int', 114),
            (70, 'sparse', 'zero_ties', 115),
            (80, 'star', 'int', 116),
            (90, 'cycle', 'float', 117),
            (100, 'sparse', 'int', 118),
            (55, 'dense', 'float', 119),
            (65, 'chain', 'zero_ties', 120),
            (75, 'star', 'float', 121),
            (85, 'sparse', 'int', 122),
            (110, 'cycle', 'zero_ties', 123),
            (120, 'sparse', 'float', 124),
            (150, 'sparse', 'int', 125),
        ]

        self.assertEqual(len(configs), 25)

        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_path = Path(tmp_dir) / 'test_graph.txt'

            for idx, (n, kind, w_type, seed) in enumerate(configs, 1):
                g = generate_test_graph(n, kind, w_type, seed)
                txt = graph_to_txt(g)
                tmp_path.write_text(txt, encoding='utf-8')

                source = g['source']
                oracle_dist, _, _ = dijkstra(g, source)

                # Chạy cả 3 thuật toán
                for algo in ['dmm25', 'chd', 'dijkstra']:
                    native = run_batch(tmp_path, source, algo)
                    v_report = verify_result(g, source, native, comparison_mode='auto')
                    self.assertTrue(
                        v_report['ok'],
                        f"Đồ thị {idx}/25 ({kind}, n={n}, {w_type}, seed={seed}) thất bại trên thuật toán {algo}: {v_report}"
                    )


if __name__ == '__main__':
    unittest.main()
