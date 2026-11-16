"""Isolated worker: no changes to upstream algorithms; validate before timing."""
from __future__ import annotations
import gc
import heapq
import importlib.util
import json
import math
from pathlib import Path
import statistics
import sys
import time
import types

ROOT = Path(__file__).resolve().parent

def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod

def oracle(n, edges):
    adj = [[] for _ in range(n)]
    for u, v, w in edges:
        adj[u].append((v, w))
    def solve():
        d = [math.inf] * n
        d[0] = 0.0
        q = [(0.0, 0)]
        while q:
            du, u = heapq.heappop(q)
            if du != d[u]:
                continue
            for v, w in adj[u]:
                nd = du + w
                if nd < d[v]:
                    d[v] = nd
                    heapq.heappush(q, (nd, v))
        return d
    return solve

def factory(mode, n, edges):
    if mode == 'python_dijkstra':
        return oracle(n, edges)
    if mode == 'nobelk':
        mod = load_module('nobelk_bmssp', ROOT/'repos/python_reference/src/sssp/bmssp.py')
        g = mod.Graph(n)
        for u,v,w in edges:
            g.add_edge(u,v,w)
        return lambda: mod.sssp(g,0)[0]
    if mode == 'logtwothirds_reference':
        mod = load_module('lt_reference', ROOT/'repos/rust_python/python/logtwothirds/_reference.py')
        g = mod.build_graph(n,edges)
        return lambda: mod.sssp(g,0)
    pkg = types.ModuleType('variant_src')
    pkg.__path__ = [str(ROOT/'repos/python_variants/src')]
    sys.modules[pkg.__name__] = pkg
    from variant_src.graph import Graph
    from variant_src.bmssp_solver import BmsspSolver, BmsspSolverV2
    g = Graph(n)
    for u,v,w in edges:
        g.add_edge(u,v,w)
    cls = BmsspSolver if mode=='bzantium_v1' else BmsspSolverV2
    def solve():
        solver = cls(g)
        solver.solve(0,n-1)
        return solver.distances
    return solve

def main():
    mode, data_path = sys.argv[1:]
    data = json.loads(Path(data_path).read_text())
    n, edges = data['n'], data['edges']
    expected = oracle(n,edges)()
    solve = factory(mode,n,edges)
    start = time.perf_counter()
    got = solve()
    warm_ms = (time.perf_counter()-start)*1000
    bad = [i for i,(a,b) in enumerate(zip(got,expected)) if not math.isclose(a,b,rel_tol=1e-9,abs_tol=1e-9)]
    if len(got)!=n or bad:
        first = bad[0] if bad else None
        print(json.dumps({'status':'WRONG','wrong_vertices':len(bad),'first_vertex':first,
                          'actual':got[first] if first is not None else None,
                          'expected':expected[first] if first is not None else None}))
        return
    samples = []
    for _ in range(3):
        gc.collect()
        # Keep GC enabled for all Python methods: allocation is part of the implementation.
        start=time.perf_counter()
        got=solve()
        samples.append((time.perf_counter()-start)*1000)
        if not all(math.isclose(a,b,rel_tol=1e-9,abs_tol=1e-9) for a,b in zip(got,expected)):
            print(json.dumps({'status':'WRONG_ON_REPEAT'})); return
    print(json.dumps({'status':'PASS','median_ms':statistics.median(samples),
                      'samples_ms':samples,'warmup_ms':warm_ms}))

if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print(json.dumps({'status':'ERROR','error':f'{type(exc).__name__}: {exc}'}))
