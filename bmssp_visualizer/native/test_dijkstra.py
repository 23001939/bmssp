"""Independent Dijkstra correctness and controlled-stream checks."""
import json
import random
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_native import ensure_dijkstra_built

class DijkstraRegression(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = str(ensure_dijkstra_built())

    def run_plain(self, n, edges, source):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'graph.txt'
            path.write_text(f'{n} {len(edges)}\n' + ''.join(f'{u} {v} {w:.17g}\n' for u,v,w in edges), encoding='ascii')
            result = subprocess.run([self.exe, str(path), str(source), 'plain'], capture_output=True, text=True, encoding='utf8', check=True, timeout=5)
            return json.loads(result.stdout)

    def test_special_cases(self):
        one = self.run_plain(1, [(0,0,0)], 0)
        self.assertEqual(one['distances'], [0])
        self.assertEqual(one['predecessors'], [None])
        edges = [(2,0,0),(0,1,3),(0,1,1),(1,0,0),(1,3,2)]
        r = self.run_plain(5, edges, 2)
        self.assertEqual(r['distances'], [0,1,0,3,None])
        self.assertEqual(r['predecessors'], [2,0,None,1,None])
        self.assertEqual(r['predecessor_edges'], [0,2,None,4,None])
        self.assertEqual(r['algorithm_id'], 'dijkstra')

    def test_random_against_bellman_ford(self):
        rng = random.Random(741)
        for _ in range(60):
            n = rng.randrange(1, 19)
            source = rng.randrange(n)
            edges = [(rng.randrange(n),rng.randrange(n),rng.choice([0, 1e-12, .5, 1, 3])) for _ in range(rng.randrange(0, n*4+1))]
            result = self.run_plain(n, edges, source)
            d = [float('inf')]*n; d[source] = 0
            for _ in range(n-1):
                changed = False
                for u,v,w in edges:
                    if d[u]+w < d[v]: d[v] = d[u]+w; changed = True
                if not changed: break
            self.assertEqual(result['distances'], [x if x != float('inf') else None for x in d])
            for v, eid in enumerate(result['predecessor_edges']):
                if eid is not None:
                    u, dest, w = edges[eid]
                    self.assertEqual((u,dest), (result['predecessors'][v],v))
                    self.assertEqual(result['distances'][u]+w,result['distances'][v])

    def test_trace_ack_and_sample(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'graph.txt'; path.write_text('3 2\n0 1 1\n1 2 1\n',encoding='ascii')
            proc=subprocess.Popen([self.exe,str(path),'0','trace'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf8')
            try:
                first=json.loads(proc.stdout.readline()); self.assertEqual(first['type'],'start')
                time.sleep(.03); self.assertIsNone(proc.poll(), 'runner must wait for next')
                events=[first]
                while events[-1]['type']!='native_result':
                    proc.stdin.write('next\n'); proc.stdin.flush()
                    events.append(json.loads(proc.stdout.readline()))
                # Result arrived after the last event ACK and must not require another ACK.
                self.assertEqual(proc.wait(timeout=3),0)
                self.assertEqual(events[-1]['distances'],[0,1,2])
                self.assertTrue({'relax','complete'} <= {e['type'] for e in events})
            finally:
                if proc.poll() is None: proc.kill(); proc.wait()
                proc.stdin.close(); proc.stdout.close(); proc.stderr.close()
            stopped=subprocess.run([self.exe,str(path),'0','trace'],input='',capture_output=True,text=True,encoding='utf8',check=True,timeout=5)
            self.assertEqual([json.loads(x)['type'] for x in stopped.stdout.splitlines()], ['start'])
            sampled=subprocess.run([self.exe,str(path),'0','trace','10000'],input='next\n',capture_output=True,text=True,encoding='utf8',check=True,timeout=5)
            events=[json.loads(x) for x in sampled.stdout.splitlines()]
            self.assertEqual([x['type'] for x in events],['start','native_result'])
            self.assertTrue(events[-1]['sampled'])
            self.assertGreater(events[-1]['events_seen'],events[-1]['events_emitted'])

if __name__ == '__main__':
    unittest.main()
