"""Black-box checks for the separately instrumented native runner."""
import json
import random
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import data  # noqa: E402
import build_native  # noqa: E402


def write_graph(path, n, edges):
    path.write_text(f"{n} {len(edges)}\n" + "".join(f"{u} {v} {w}\n" for u, v, w in edges), encoding="utf-8")


class NativeRunnerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runner = Path(build_native.ensure_built())

    def run_plain(self, path, source, timeout=15):
        completed = subprocess.run([str(self.runner), str(path), str(source), "plain"],
                                   capture_output=True, text=True, timeout=timeout, check=True)
        rows = [json.loads(line) for line in completed.stdout.splitlines() if line.strip()]
        self.assertEqual(len(rows), 1, completed.stdout)
        return rows[0]

    def compare(self, n, edges, source):
        graph = data.normalize_graph({"n": n, "edges": edges})
        expected = data.dijkstra(graph, source)[0]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "graph.txt"
            write_graph(path, n, edges)
            result = self.run_plain(path, source)
        self.assertEqual(result["distances"], expected)

    def test_explicit_zero_ties_parallel_cycle_and_unreachable(self):
        self.compare(7, [(0, 1, 0), (0, 1, 5), (1, 2, 2), (0, 2, 2),
                         (2, 3, 1), (3, 1, 0), (4, 5, 1)], 0)
        self.compare(5, [(0, 2, 4), (1, 2, 1), (2, 4, 0)], 1)

    def test_seeded_small_random_graphs_against_python_dijkstra(self):
        rng = random.Random(20261113)
        for case in range(10):
            n = rng.randint(2, 14)
            edges = [(rng.randrange(n), rng.randrange(n), rng.choice([0, 1, 2, 3, 7]))
                     for _ in range(rng.randrange(n, n * 4))]
            self.compare(n, edges, rng.randrange(n))

    def test_trace_waits_for_ack_and_terminal_result_needs_no_ack(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "small.txt"
            write_graph(path, 3, [(0, 1, 1), (1, 2, 1)])
            proc = subprocess.Popen([str(self.runner), str(path), "0", "trace"],
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, text=True, bufsize=1)
            try:
                first = json.loads(proc.stdout.readline())
                self.assertEqual(first["type"], "start")
                # The next line must not arrive before the controller ACK.
                import threading
                received = []
                waiter = threading.Thread(target=lambda: received.append(proc.stdout.readline()), daemon=True)
                waiter.start()
                waiter.join(0.15)
                self.assertFalse(received, "runner emitted an event before ACK")
                proc.stdin.write("next\n")
                proc.stdin.flush()
                waiter.join(3)
                self.assertTrue(received and received[0], "runner did not resume after ACK")
                # Drain with ACKs until native_result; terminal event must not block for another ACK.
                events = [first, json.loads(received[0])]
                while events[-1].get("type") != "native_result":
                    proc.stdin.write("next\n")
                    proc.stdin.flush()
                    line = proc.stdout.readline()
                    self.assertTrue(line, "runner exited before native_result")
                    events.append(json.loads(line))
                self.assertEqual(proc.wait(timeout=3), 0)
                self.assertEqual(events[-1]["distances"], [0, 1, 2])
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait(timeout=3)
                for stream in (proc.stdin, proc.stdout, proc.stderr):
                    if stream is not None:
                        stream.close()


if __name__ == "__main__":
    unittest.main()
