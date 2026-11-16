"""HTTP integration tests; use only loopback and ephemeral ports."""
import json
import sys
import threading
import time
import unittest
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import server  # noqa: E402
import data  # noqa: E402


class ServerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.httpd = server.create_server(host="127.0.0.1", port=0)
        cls.thread = threading.Thread(target=cls.httpd.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = f"http://127.0.0.1:{cls.httpd.server_address[1]}"

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()
        cls.thread.join(timeout=3)

    def request(self, path, payload=None):
        data = None if payload is None else json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(self.base + path, data=data,
                                     headers={"Content-Type": "application/json"} if data else {})
        with urllib.request.urlopen(req, timeout=10) as response:
            return response.status, response.headers, response.read()

    def wait_for_run(self, run_id, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            _, _, body = self.request("/api/runs/" + run_id)
            run = json.loads(body)
            if run.get("result") is not None or run.get("status") in ("error", "failed", "stopped"):
                return run
            time.sleep(0.05)
        self.fail(f"run {run_id} did not finish within {timeout}s")

    def read_sse(self, run_id):
        with urllib.request.urlopen(self.base + f"/api/runs/{run_id}/events", timeout=15) as response:
            self.assertTrue(response.headers.get_content_type() == "text/event-stream")
            events = []
            for raw in response:
                line = raw.decode("utf-8").strip()
                if line.startswith("data: "):
                    events.append(json.loads(line[6:]))
            return events

    def stream_to_queue(self, run_id, target):
        with urllib.request.urlopen(self.base + f"/api/runs/{run_id}/events", timeout=15) as response:
            for raw in response:
                line = raw.decode("utf-8").strip()
                if line.startswith("data: "):
                    target.put(json.loads(line[6:]))

    def test_datasets_and_upload_text_graph(self):
        _, _, body = self.request("/api/datasets")
        self.assertIsInstance(json.loads(body)["datasets"], list)
        _, _, body = self.request("/api/graphs", {"text": "3 2\n0 1 1\n1 2 0\n", "format": "txt"})
        uploaded = json.loads(body)
        self.assertEqual(uploaded["graph"]["n"], 3)
        _, _, body = self.request("/api/graph?id=" + uploaded["id"])
        self.assertEqual(json.loads(body)["edges"][1]["w"], 0)

    def test_weighted_100_dataset_is_sparse_stable_and_fully_reachable(self):
        _, _, body = self.request("/api/datasets")
        datasets = json.loads(body)["datasets"]
        entry = next((item for item in datasets if item["id"] == "weighted_100"), None)
        self.assertIsNotNone(entry, "weighted_100 should be listed in the built-in dataset catalog")
        self.assertEqual(entry["n"], 100)
        _, _, body = self.request("/api/graph?id=weighted_100")
        graph = json.loads(body)
        _, _, repeat_body = self.request("/api/graph?id=weighted_100")
        repeated = json.loads(repeat_body)
        self.assertEqual(graph["n"], 100)
        self.assertEqual(graph["edges"], repeated["edges"], "dataset weights/edge IDs must be stable")
        self.assertEqual(graph["source"], 0)
        self.assertEqual(graph["target"], 99)
        self.assertLess(len(graph["edges"]), graph["n"] * 10, "dataset should remain sparse")
        self.assertEqual(len({edge["id"] for edge in graph["edges"]}), len(graph["edges"]))
        distances, _, _ = data.dijkstra(graph, 0)
        self.assertTrue(all(distance is not None for distance in distances))
        self.assertEqual(graph["metadata"].get("seed"), 20261114)

    def test_both_plain_algorithms_agree_on_weighted_100(self):
        results = {}
        run_ids = {}
        graph_hashes = {}
        for algorithm in ("dijkstra", "bmssp"):
            _, _, body = self.request("/api/runs", {"graph_id": "weighted_100", "source": 0,
                                                      "mode": "fast", "delay_ms": 0,
                                                      "algorithm": algorithm})
            run_id = json.loads(body)["run_id"]
            run_ids[algorithm] = run_id
            run = self.wait_for_run(run_id)
            self.assertEqual(run["status"], "completed")
            self.assertTrue(run["result"]["verified"])
            self.assertEqual(run["result"]["algorithm_id"], algorithm)
            graph_hashes[algorithm] = run["result"]["graph_hash"]
            results[algorithm] = run["result"]["distances"]
        self.assertEqual(results["dijkstra"], results["bmssp"])
        self.assertEqual(graph_hashes['dijkstra'],graph_hashes['bmssp'])

    def test_unknown_algorithm_is_rejected(self):
        with self.assertRaises(urllib.error.HTTPError) as invalid:
            self.request("/api/runs", {"graph_id": "weighted_100", "source": 0,
                                        "mode": "fast", "algorithm": "mystery"})
        self.assertEqual(invalid.exception.code, 400)
        self.assertIn("error", json.loads(invalid.exception.read()))

    def test_fast_run_result_and_exports_are_consistent(self):
        _, _, body = self.request("/api/graphs", {"graph": {"name": "demo </script><script>alert(1)</script>",
                                                               "n": 4, "edges": [[0, 1, 2], [1, 2, 0]]}})
        graph_id = json.loads(body)["id"]
        _, _, body = self.request("/api/runs", {"graph_id": graph_id, "source": 0,
                                                  "mode": "fast", "delay_ms": 0})
        run_id = json.loads(body)["run_id"]
        run = self.wait_for_run(run_id)
        self.assertIn(run["status"], ("complete", "completed", "done"))
        self.assertTrue(run["result"]["verified"])
        self.assertEqual(run["result"]["distances"], [0, 2, 2, None])
        for kind in ("graph", "result", "trace", "csv", "html"):
            _, headers, body = self.request(f"/api/runs/{run_id}/export?kind={kind}")
            self.assertGreater(len(body), 0, kind)
            if kind == "html":
                html = body.decode("utf-8")
                self.assertIn("offline-data", html)
                self.assertNotIn("</script><script>alert", html.lower())
                self.assertIn("\\u003c/script", html)

    def test_unknown_ids_and_invalid_upload_return_json_errors(self):
        with self.assertRaises(urllib.error.HTTPError) as missing:
            self.request("/api/graph?id=does-not-exist")
        self.assertEqual(missing.exception.code, 404)
        self.assertIn("error", json.loads(missing.exception.read()))
        with self.assertRaises(urllib.error.HTTPError) as invalid:
            self.request("/api/graphs", {"graph": {"n": 2, "edges": [[0, 9, 1]]}})
        self.assertEqual(invalid.exception.code, 400)

    def test_run_events_are_scoped_to_run_id(self):
        _, _, body = self.request("/api/graphs", {"graph": {"n": 2, "edges": [[0, 1, 1]]}})
        graph_id = json.loads(body)["id"]
        ids = []
        for _ in range(2):
            _, _, body = self.request("/api/runs", {"graph_id": graph_id, "source": 0, "mode": "fast", "delay_ms": 0})
            ids.append(json.loads(body)["run_id"])
            self.wait_for_run(ids[-1])
        self.assertNotEqual(ids[0], ids[1])
        chunks = []
        for run_id in ids:
            chunks.append(self.read_sse(run_id))
        self.assertTrue(chunks[0])
        self.assertTrue(chunks[1])
        self.assertEqual({e["run_id"] for e in chunks[0]}, {ids[0]})
        self.assertEqual({e["run_id"] for e in chunks[1]}, {ids[1]})

    def test_controlled_pause_step_resume_and_stop(self):
        # A modest chain guarantees enough real native events while keeping the
        # full resume path practical for a local integration test.
        edges = [[i, i + 1, 1] for i in range(39)]
        _, _, body = self.request("/api/graphs", {"graph": {"n": 40, "edges": edges}})
        graph_id = json.loads(body)["id"]
        _, _, body = self.request("/api/runs", {"graph_id": graph_id, "source": 0,
                                                  "mode": "controlled", "delay_ms": 0})
        run_id = json.loads(body)["run_id"]
        import queue
        event_queue = queue.Queue()
        reader = threading.Thread(target=self.stream_to_queue, args=(run_id, event_queue), daemon=True)
        reader.start()
        self.request(f"/api/runs/{run_id}/control", {"action": "pause"})
        deadline = time.monotonic() + 5
        observed = []
        while time.monotonic() < deadline and len(observed) < 2:
            try:
                observed.append(event_queue.get(timeout=0.05))
            except queue.Empty:
                pass
        self.assertGreaterEqual(len(observed), 2, "controlled runner did not emit initial events")
        algorithm_before = sum(e["type"] != "status" for e in observed)
        time.sleep(0.15)
        _, _, body = self.request(f"/api/runs/{run_id}")
        count_before = json.loads(body)["events_count"]
        time.sleep(0.1)
        _, _, body = self.request(f"/api/runs/{run_id}")
        self.assertEqual(json.loads(body)["events_count"], count_before,
                         "paused run advanced without an ACK")
        self.request(f"/api/runs/{run_id}/control", {"action": "step"})
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            try:
                observed.append(event_queue.get(timeout=0.05))
            except queue.Empty:
                pass
            count = sum(e["type"] != "status" for e in observed)
            if count > algorithm_before:
                break
        else:
            self.fail("step did not release one native event")
        self.assertEqual(count, algorithm_before + 1, "one step should release exactly one native event")
        _, _, body = self.request(f"/api/runs/{run_id}")
        self.assertEqual(json.loads(body)["status"], "paused")
        self.request(f"/api/runs/{run_id}/control", {"action": "resume"})
        completed = self.wait_for_run(run_id)
        self.assertIsNotNone(completed["result"])
        reader.join(timeout=5)

        # Stop uses a separate controlled run; it must become terminal and must
        # not later publish a result from an already-cancelled subprocess.
        _, _, body = self.request("/api/runs", {"graph_id": graph_id, "source": 0,
                                                  "mode": "controlled", "delay_ms": 0})
        stopped_id = json.loads(body)["run_id"]
        self.request(f"/api/runs/{stopped_id}/control", {"action": "pause"})
        self.request(f"/api/runs/{stopped_id}/control", {"action": "stop"})
        stopped = self.wait_for_run(stopped_id)
        self.assertEqual(stopped["status"], "stopped")
        self.assertIsNone(stopped["result"])

    def test_dijkstra_controlled_pause_step_and_resume(self):
        _, _, body = self.request("/api/runs", {"graph_id": "weighted_100", "source": 0,
                                                  "mode": "controlled", "delay_ms": 0,
                                                  "algorithm": "dijkstra"})
        run_id = json.loads(body)["run_id"]
        import queue
        event_queue = queue.Queue()
        reader = threading.Thread(target=self.stream_to_queue, args=(run_id, event_queue), daemon=True)
        reader.start()
        self.request(f"/api/runs/{run_id}/control", {"action": "pause"})
        observed = []
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and len(observed) < 2:
            try:
                observed.append(event_queue.get(timeout=0.05))
            except queue.Empty:
                pass
        self.assertGreaterEqual(len(observed), 2, "Dijkstra did not emit its initial controlled events")
        algorithm_count = sum(e["type"] != "status" for e in observed)
        _, _, body = self.request(f"/api/runs/{run_id}")
        held_count = json.loads(body)["events_count"]
        time.sleep(0.1)
        _, _, body = self.request(f"/api/runs/{run_id}")
        self.assertEqual(json.loads(body)["events_count"], held_count, "paused Dijkstra advanced without a step")
        self.request(f"/api/runs/{run_id}/control", {"action": "step"})
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            try:
                observed.append(event_queue.get(timeout=0.05))
            except queue.Empty:
                pass
            count = sum(e["type"] != "status" for e in observed)
            if count > algorithm_count:
                break
        self.assertEqual(count, algorithm_count + 1, "a Dijkstra step should release exactly one algorithm event")
        self.request(f"/api/runs/{run_id}/control", {"action": "resume"})
        run = self.wait_for_run(run_id)
        self.assertEqual(run["result"]["algorithm_id"], "dijkstra")
        self.assertTrue(run["result"]["verified"])
        reader.join(timeout=5)


if __name__ == "__main__":
    unittest.main()
