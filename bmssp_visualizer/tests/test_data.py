"""Contract tests for graph input normalization and independent verification."""
import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import data  # noqa: E402


class GraphDataTests(unittest.TestCase):
    def test_normalizes_parallel_edges_with_stable_ids_and_positions(self):
        graph = data.normalize_graph({"n": 3, "edges": [[0, 1, 9], [0, 1, 2], [1, 2, 0]]})
        self.assertEqual([e["id"] for e in graph["edges"]], [0, 1, 2])
        self.assertEqual(graph["n"], 3)
        self.assertEqual(len(graph["positions"]), 3)
        for p in graph["positions"]:
            self.assertTrue(math.isfinite(p["x"]) and math.isfinite(p["y"]))
            self.assertGreaterEqual(p["x"], 0)
            self.assertGreaterEqual(p["y"], 0)

    def test_rejects_invalid_graphs(self):
        invalid = [
            {"n": 0, "edges": []},
            {"n": 2, "edges": [[0, 2, 1]]},
            {"n": 2, "edges": [[-1, 1, 1]]},
            {"n": 2, "edges": [[0, 1, -0.1]]},
            {"n": 2, "edges": [[0, 1, float("inf")]]},
            {"n": 2, "edges": [[0, 1, float("nan")]]},
        ]
        for raw in invalid:
            with self.subTest(raw=raw), self.assertRaises((ValueError, TypeError)):
                data.normalize_graph(raw)

    def test_txt_parser_checks_header_and_edge_count(self):
        graph = data.parse_text("4 2\n0 1 0\n1 3 2.5\n", "txt")
        self.assertEqual(graph["n"], 4)
        self.assertEqual(len(graph["edges"]), 2)
        with self.assertRaises((ValueError, TypeError)):
            data.parse_text("2 2\n0 1 1\n", "txt")

    def test_json_parser(self):
        graph = data.parse_text('{"n":2,"edges":[[0,1,3]]}', "json", name="input.json")
        self.assertEqual(graph["name"], "input.json")
        self.assertEqual(graph["edges"][0]["w"], 3)

    def test_dijkstra_handles_zero_ties_parallel_edges_unreachable_and_source(self):
        graph = data.normalize_graph({"n": 6, "edges": [
            [0, 1, 4], [0, 1, 1], [1, 2, 0], [0, 2, 1],
            [2, 3, 2], [1, 3, 2], [4, 5, 1],
        ]})
        dist, pred, pred_edges = data.dijkstra(graph, 1)
        self.assertEqual(dist, [None, 0, 0, 2, None, None])
        self.assertEqual(pred[1], None)
        self.assertEqual(pred_edges[1], None)
        self.assertIn(pred[2], (0, 1))
        self.assertIn(pred[3], (1, 2))

    def test_verifier_accepts_correct_result_and_rejects_wrong_distance(self):
        graph = data.normalize_graph({"n": 4, "edges": [[0, 1, 2], [1, 2, 0]]})
        good = {"distances": [0, 2, 2, None], "predecessors": [None, 0, 1, None],
                "predecessor_edges": [None, 0, 1, None]}
        self.assertTrue(data.verify_result(graph, 0, good)["ok"])
        bad = dict(good, distances=[0, 2, 3, None])
        self.assertFalse(data.verify_result(graph, 0, bad)["ok"])

    def test_near_tie_path_exact_and_tolerance_are_separate(self):
        graph = data.normalize_graph({"n": 3, "edges": [[0,1,4e-11], [0,2,0], [2,1,0]]})
        wrong = {"distances": [0,0,0], "predecessors": [None,0,0]}
        exact = data.verify_result(graph, 0, dict(wrong), comparison_mode='exact')
        self.assertFalse(exact['ok'])
        self.assertTrue(exact['distance_exact_ok'])
        self.assertFalse(exact['path_exact_ok'])
        self.assertTrue(exact['path_tolerance_ok'])
        correct = {"distances": [0,0,0], "predecessors": [None,2,0]}
        self.assertTrue(data.verify_result(graph, 0, correct, comparison_mode='exact')['ok'])
        self.assertEqual(correct['predecessor_edges'][1], 2)

    def test_auto_integer_mode_and_reachability_mismatch(self):
        graph = data.normalize_graph({"n": 3, "edges": [[0,1,1]]})
        result = {"distances": [0,1,None], "predecessors": [None,0,None]}
        report = data.verify_result(graph, 0, result)
        self.assertEqual(report['comparison_mode'], 'exact')
        self.assertTrue(report['integer_exact_domain'])
        self.assertTrue(report['path_exact_ok'])
        result['distances'][2] = 0
        report = data.verify_result(graph, 0, result)
        self.assertFalse(report['ok'])
        self.assertEqual(report['exact_mismatches'], 1)

    def test_verifier_rejects_predecessor_cycle_even_when_distances_are_correct(self):
        graph = data.normalize_graph({"n": 3, "edges": [[0, 1, 1], [0, 2, 1], [1, 2, 0], [2, 1, 0]]})
        cyclic = {"distances": [0, 1, 1], "predecessors": [None, 2, 1]}
        report = data.verify_result(graph, 0, cyclic)
        self.assertFalse(report["ok"])
        self.assertTrue(report["cycle_vertices"])


if __name__ == "__main__":
    unittest.main()
