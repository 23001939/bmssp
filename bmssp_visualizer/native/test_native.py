"""Standalone regressions for native runner; run: python -m unittest discover -s bmssp_visualizer/native -p 'test_*.py'."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_native import ensure_baseline_built, ensure_built

class NativeRegression(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runner, cls.baseline = ensure_built(), ensure_baseline_built()

    def invoke(self, text, source=0, sample=1):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'graph.txt'
            path.write_text(text, encoding='ascii')
            baseline = subprocess.run([str(self.baseline), str(path), str(source)], text=True, capture_output=True, check=True, timeout=5)
            plain = subprocess.run([str(self.runner), str(path), str(source), 'plain'], text=True, capture_output=True, check=True, timeout=5)
            proc = subprocess.Popen([str(self.runner), str(path), str(source), 'trace', str(sample)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            events = []
            try:
                for line in proc.stdout:
                    event = json.loads(line)
                    events.append(event)
                    if event['type'] == 'native_result':
                        break
                    proc.stdin.write('next\n')
                    proc.stdin.flush()
                proc.stdin.close()
                self.assertEqual(proc.wait(timeout=5), 0)
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
                proc.stdout.close()
                proc.stderr.close()
            return json.loads(baseline.stdout), json.loads(plain.stdout), events

    def test_nonzero_source_parallel_zero_unreachable(self):
        base, plain, events = self.invoke('5 5\n2 0 0\n0 1 1\n0 1 3\n1 0 0\n1 3 2\n', 2)
        self.assertEqual(base['distances'], [0, 1, 0, 3, None])
        self.assertEqual(base['distances'], plain['distances'])
        self.assertEqual(base['distances'], events[-1]['distances'])
        self.assertIsNone(plain['predecessors'][2])
        self.assertEqual(plain['predecessor_unresolved'], 0)
        self.assertTrue({'enter', 'pivots', 'relax', 'complete', 'exit'} <= {e['type'] for e in events})

    def test_single_vertex(self):
        base, plain, events = self.invoke('1 1\n0 0 0\n')
        self.assertEqual(base['distances'], [0])
        self.assertEqual(plain['distances'], base['distances'])
        self.assertEqual(events[-1]['distances'], base['distances'])

    def test_sampled_result_is_not_suppressed(self):
        base, plain, events = self.invoke('3 2\n0 1 1\n1 2 1\n', sample=10000)
        self.assertEqual([e['type'] for e in events], ['start', 'native_result'])
        self.assertEqual(events[-1]['distances'], base['distances'])
        self.assertTrue(events[-1]['sampled'])
        self.assertGreater(events[-1]['events_seen'], events[-1]['events_emitted'])

    def test_trace_complete_sets_against_small_oracle(self):
        # Includes equal-cost paths, a zero cycle, parallel edges and an unreachable vertex.
        graph = '7 8\n1 0 0\n1 2 1\n0 2 1\n2 0 0\n0 3 2\n2 3 1\n3 4 0\n1 2 3\n'
        expected = [0,0,1,2,2,None,None]
        _, _, events = self.invoke(graph, source=1)
        tentative = [None]*7
        tentative[1] = 0
        complete_count = 0
        for event in events:
            if event['type'] == 'relax': tentative[event['v']] = event['distance']
            if event['type'] == 'complete':
                complete_count += 1
                vertices = event['vertices']
                self.assertEqual(len(vertices), len(set(vertices)))
                for vertex in vertices:
                    self.assertEqual(tentative[vertex], expected[vertex])
                    if event.get('bound') is not None:
                        # The internal bound is a tuple; tied scalar distances are possible.
                        self.assertLessEqual(tentative[vertex], event['bound'])
        self.assertGreater(complete_count, 0)

    def test_transformed_isolated_self_loop_parallel_and_mapping(self):
        source = Path(__file__).with_name('test_transform.cpp')
        with tempfile.TemporaryDirectory() as temp:
            exe = Path(temp) / 'transform.exe'
            subprocess.run(['g++', '-std=c++20', '-O1', '-D_GLIBCXX_DEBUG', str(source), '-o', str(exe)], check=True, timeout=30)
            subprocess.run([str(exe)], check=True, timeout=5)

    def test_batch_queue_contract_with_debug_iterators(self):
        source = Path(__file__).with_name('test_batchpq.cpp')
        with tempfile.TemporaryDirectory() as temp:
            exe = Path(temp) / 'batchpq.exe'
            subprocess.run(['g++', '-std=c++20', '-O1', '-D_GLIBCXX_DEBUG', str(source), '-o', str(exe)], check=True, timeout=30)
            subprocess.run([str(exe)], check=True, timeout=5)

    def test_known_upstream_near_tie(self):
        # The true shortest distance to 1 is zero via 2; upstream rounds comparison keys to 1e-10.
        base, plain, events = self.invoke('3 3\n0 1 0.00000000004\n0 2 0\n2 1 0\n')
        self.assertEqual(base['distances'][1], 4e-11)
        self.assertEqual(plain['distances'][1], 0)
        self.assertEqual(plain['predecessors'], [None, 2, 0])
        self.assertIn(plain['predecessor_origin'], ['solver', 'exact_edge_reconstruction'])
        self.assertEqual(events[-1]['distances'], plain['distances'])
        self.assertFalse(plain['comparison_rounding'])
        self.assertIn('real_distance_comparison', plain['patches'])

if __name__ == '__main__':
    unittest.main()
