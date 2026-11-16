"""Cross-algorithm consistency test suite for DMM+25, C-HD, and Dijkstra.

Runs all 3 algorithms on multiple graph structures and verifies distances
match 100% with the independent Python Dijkstra Oracle.
"""
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from typing import Any, Dict, List

from bmssp_visualizer_mizchi.data import (
    demo_graph,
    weighted_graph_100,
    normalize_graph,
    dijkstra as dijkstra_oracle,
    verify_result,
)
from bmssp_visualizer_mizchi.runner import run_batch

ALGORITHMS = ('dmm25', 'chd', 'dijkstra')


class TestCrossAlgorithms(unittest.TestCase):
    def run_all_solvers(self, graph: Dict[str, Any], source: int = 0) -> List[Dict[str, Any]]:
        with tempfile.NamedTemporaryFile('w', encoding='utf-8', suffix='.txt', delete=False) as f:
            f.write(f"{graph['n']} {len(graph['edges'])}\n")
            for e in graph['edges']:
                f.write(f"{e['u']} {e['v']} {e['w']:.17g}\n")
            temp_path = Path(f.name)

        results = []
        try:
            for algo in ALGORITHMS:
                res = run_batch(temp_path, source, algo)
                verification = verify_result(graph, source, res)
                self.assertTrue(
                    verification['ok'],
                    f"Algorithm {algo} failed Dijkstra Oracle verification: {verification.get('first_wrong_vertices')}"
                )
                results.append(res)
        finally:
            if temp_path.exists():
                temp_path.unlink()

        # Cross-compare exact distances across all 3 algorithms
        base_distances = results[0]['distances']
        for i in range(1, len(results)):
            other_algo = results[i]['algorithm']
            other_distances = results[i]['distances']
            self.assertEqual(
                base_distances,
                other_distances,
                f"Distances mismatch between {results[0]['algorithm']} and {other_algo}"
            )
        return results

    def test_01_demo_graph(self):
        graph = demo_graph()
        results = self.run_all_solvers(graph, source=0)
        self.assertEqual(len(results), 3)
        self.assertIsNone(results[0]['distances'][17], "Vertex 17 must be unreachable")
        self.assertEqual(results[0]['distances'][16], 11.0)

    def test_02_weighted_100(self):
        graph = weighted_graph_100()
        results = self.run_all_solvers(graph, source=0)
        self.assertEqual(len(results), 3)
        # All 100 vertices are guaranteed reachable
        self.assertTrue(all(d is not None for d in results[0]['distances']))

    def test_03_zero_weight_and_ties(self):
        edges = [
            [0, 1, 0.0],
            [1, 2, 0.0],
            [2, 3, 5.0],
            [0, 3, 5.0],  # Exact tie to node 3
            [3, 4, 2.0],
            [4, 5, 0.0],
        ]
        graph = normalize_graph({'n': 6, 'source': 0, 'edges': edges})
        results = self.run_all_solvers(graph, source=0)
        self.assertEqual(results[0]['distances'], [0.0, 0.0, 0.0, 5.0, 7.0, 7.0])

    def test_04_parallel_edges(self):
        edges = [
            [0, 1, 10.0],
            [0, 1, 3.0],   # Shortest parallel edge
            [0, 1, 7.0],
            [1, 2, 4.0],
            [1, 2, 2.0],   # Shortest parallel edge
            [0, 2, 6.0],   # Direct edge (w=6 > 3+2=5)
            [2, 3, 1.0],
            [4, 5, 2.0],   # Disconnected component 4->5
        ]
        graph = normalize_graph({'n': 6, 'source': 0, 'edges': edges})
        results = self.run_all_solvers(graph, source=0)
        self.assertEqual(results[0]['distances'], [0.0, 3.0, 5.0, 6.0, None, None])

    def test_05_tree_dag(self):
        edges = [
            [0, 1, 10.0], [0, 2, 20.0],
            [1, 3, 5.0],  [1, 4, 15.0],
            [2, 5, 2.0],  [2, 6, 8.0],
            [3, 7, 1.0],
        ]
        graph = normalize_graph({'n': 8, 'source': 0, 'edges': edges})
        results = self.run_all_solvers(graph, source=0)
        oracle_dist, _, _ = dijkstra_oracle(graph, 0)
        self.assertEqual(results[0]['distances'], oracle_dist)


if __name__ == '__main__':
    unittest.main(verbosity=2)
