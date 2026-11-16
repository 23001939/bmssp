"""Validated graph interchange and independent shortest-path verification."""
from __future__ import annotations

import heapq
import json
import math
import random

MAX_N = 500_000
MAX_EDGES = 1_500_000


def integer(value, label, minimum=0, maximum=None):
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f'{label} phải là số nguyên.')
    if value < minimum or (maximum is not None and value > maximum):
        raise ValueError(f'{label} ngoài giới hạn cho phép ({minimum}–{maximum}).')
    return value


def finite(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f'{label} phải là số hữu hạn.')
    try:
        number = float(value)
    except (OverflowError, ValueError):
        raise ValueError(f'{label} phải là số hữu hạn.') from None
    if not math.isfinite(number):
        raise ValueError(f'{label} phải là số hữu hạn.')
    return number


def layout(n):
    if n == 1:
        return [{'x': 500.0, 'y': 350.0}]
    if n <= 80:
        cols = max(2, math.ceil(math.sqrt(n * 1.6)))
        rows = math.ceil(n / cols)
        return [{'x': 90 + (i % cols) * 820 / max(1, cols-1),
                 'y': 75 + (i // cols) * 550 / max(1, rows-1)} for i in range(n)]
    # Deterministic layout for partial views, O(n), no force-simulation on 50k vertices.
    return [{'x': 80 + ((i * 0.61803398875) % 1) * 840,
             'y': 70 + ((i * 0.41421356237) % 1) * 560} for i in range(n)]


def normalize_graph(raw, name=None):
    if not isinstance(raw, dict):
        raise ValueError('Đồ thị JSON phải là một object.')
    if 'graph' in raw and isinstance(raw['graph'], dict):
        raw = raw['graph']
    n = integer(raw.get('n'), 'Số đỉnh', 1, MAX_N)
    source = integer(raw.get('source', 0), 'Đỉnh nguồn', 0, n-1)
    target = raw.get('target', n-1)
    if target is not None:
        target = integer(target, 'Đỉnh đích', 0, n-1)
    original = raw.get('edges')
    if not isinstance(original, list) or len(original) > MAX_EDGES:
        raise ValueError(f'Danh sách cạnh không hợp lệ hoặc vượt {MAX_EDGES:,} cạnh.')
    edges, seen = [], set()
    for i, edge in enumerate(original):
        if isinstance(edge, dict):
            u, v, w, eid = edge.get('u'), edge.get('v'), edge.get('w'), edge.get('id', i)
        elif isinstance(edge, (list, tuple)) and len(edge) == 3:
            u, v, w = edge
            eid = i
        else:
            raise ValueError(f'Cạnh {i} phải có u, v, w.')
        integer(u, f'Đầu cạnh {i}', 0, n-1)
        integer(v, f'Cuối cạnh {i}', 0, n-1)
        integer(eid, f'ID cạnh {i}', 0, 2**53-1)
        if eid in seen:
            raise ValueError(f'ID cạnh {eid} bị trùng.')
        seen.add(eid)
        weight = finite(w, f'Trọng số cạnh {i}')
        if weight < 0 or weight > 1e100:
            raise ValueError('BMSSP cần trọng số không âm, tối đa 1e100 trong demo này.')
        edges.append({'id': eid, 'u': u, 'v': v, 'w': weight})
    positions = raw.get('positions')
    if positions is None:
        positions = layout(n)
    else:
        if not isinstance(positions, list) or len(positions) != n:
            raise ValueError('positions phải có đúng một tọa độ cho mỗi đỉnh.')
        checked = []
        for pos in positions:
            if isinstance(pos, dict):
                x, y = pos.get('x'), pos.get('y')
            elif isinstance(pos, (list, tuple)) and len(pos) == 2:
                x, y = pos
            else:
                raise ValueError('Tọa độ phải có x và y.')
            x, y = finite(x, 'Tọa độ x'), finite(y, 'Tọa độ y')
            if abs(x) > 1e7 or abs(y) > 1e7:
                raise ValueError('Tọa độ vượt giới hạn hiển thị.')
            checked.append({'x': x, 'y': y})
        positions = checked
    metadata = dict(raw.get('metadata', {})) if isinstance(raw.get('metadata', {}), dict) else {}
    for key in ('seed', 'kind'):
        if key in raw:
            metadata[key] = raw[key]
    return {'id': str(raw.get('id', 'custom'))[:100],
            'name': str(name or raw.get('name') or 'Đồ thị đã nhập')[:160],
            'n': n, 'source': source, 'target': target, 'edges': edges,
            'positions': positions, 'metadata': metadata}


def parse_text(text, fmt='txt', name=None):
    if not isinstance(text, str):
        raise ValueError('Dữ liệu nhập phải là văn bản.')
    if fmt.lower() == 'json':
        try:
            raw = json.loads(text)
        except (ValueError, TypeError) as exc:
            raise ValueError(f'JSON không hợp lệ: {exc}') from None
        return normalize_graph(raw, name)
    if fmt.lower() != 'txt':
        raise ValueError('Chỉ hỗ trợ TXT hoặc JSON.')
    lines = [line.strip() for line in text.splitlines()
             if line.strip() and not line.lstrip().startswith('#')]
    if not lines:
        raise ValueError('File đầu vào trống.')
    try:
        head = lines[0].split()
        if len(head) != 2:
            raise ValueError('Dòng đầu cần n m.')
        n, m = map(int, head)
        if m < 0 or m > MAX_EDGES or len(lines)-1 != m:
            raise ValueError('Số dòng cạnh không khớp m hoặc vượt giới hạn.')
        edges = []
        for line in lines[1:]:
            parts = line.split()
            if len(parts) != 3:
                raise ValueError('Mỗi dòng cạnh cần u v w.')
            edges.append([int(parts[0]), int(parts[1]), float(parts[2])])
    except (OverflowError, ValueError) as exc:
        raise ValueError(f'TXT không hợp lệ: {exc}') from None
    return normalize_graph({'n': n, 'edges': edges}, name)


def dijkstra(graph, source):
    n = graph['n']
    integer(source, 'Đỉnh nguồn', 0, n-1)
    adj = [[] for _ in range(n)]
    for edge in graph['edges']:
        adj[edge['u']].append((edge['v'], edge['w'], edge['id']))
    dist, pred, pe = [math.inf]*n, [None]*n, [None]*n
    dist[source] = 0.0
    heap = [(0.0, source)]
    while heap:
        value, u = heapq.heappop(heap)
        if value != dist[u]:
            continue
        for v, w, eid in adj[u]:
            new = value + w
            if new < dist[v]:
                dist[v], pred[v], pe[v] = new, u, eid
                heapq.heappush(heap, (new, v))
    return [x if math.isfinite(x) else None for x in dist], pred, pe


def verify_result(graph, source, result, comparison_mode='auto'):
    """Verify distances and source-rooted paths separately, in binary64 arithmetic.

    auto uses exact comparison for integer graphs whose simple-path sums fit in
    binary64's exact integer range, and tolerance comparison otherwise. The
    explicit exact mode checks binary64 equality, not arbitrary real arithmetic.
    """
    if comparison_mode not in ('auto', 'exact', 'tolerance'):
        raise ValueError('Comparison mode must be auto, exact or tolerance.')
    expected, _, _ = dijkstra(graph, source)
    distances = result.get('distances', [])
    predecessors = result.get('predecessors', [])
    n = graph['n']
    weights = [edge['w'] for edge in graph['edges']]
    integer_exact = (all(float(w).is_integer() for w in weights)
                     and int(max(weights, default=0)) * max(0, n-1) <= 2**53)
    mode = ('exact' if integer_exact else 'tolerance') if comparison_mode == 'auto' else comparison_mode
    if not isinstance(distances, list) or not isinstance(predecessors, list) or len(distances) != n or len(predecessors) != n:
        return {'ok': False, 'error': 'Result array lengths do not match vertex count.',
                'wrong_vertices': n, 'comparison_mode': mode,
                'distance_exact_ok': False, 'distance_tolerance_ok': False,
                'path_exact_ok': False, 'path_tolerance_ok': False}

    def finite_number(x):
        return isinstance(x, (int, float)) and not isinstance(x, bool) and math.isfinite(x)

    exact_bad, tolerance_bad = [], []
    max_abs_error = 0.0
    for v, (actual, want) in enumerate(zip(distances, expected)):
        equal = (actual is None) if want is None else finite_number(actual) and actual == want
        close = (actual is None) if want is None else finite_number(actual) and math.isclose(actual, want, rel_tol=1e-9, abs_tol=1e-9)
        if not equal: exact_bad.append(v)
        if not close: tolerance_bad.append(v)
        if want is not None and finite_number(actual):
            max_abs_error = max(max_abs_error, abs(actual-want))

    incoming = {}
    for edge in graph['edges']:
        incoming.setdefault((edge['u'], edge['v']), []).append(edge)
    predecessor_edges = [None]*n
    predecessor_errors = []
    for v, want in enumerate(expected):
        parent = predecessors[v]
        if v == source or want is None:
            if parent is not None: predecessor_errors.append(v)
            continue
        if (isinstance(parent, bool) or not isinstance(parent, int) or not 0 <= parent < n
                or not finite_number(distances[parent]) or not finite_number(distances[v])):
            predecessor_errors.append(v)
            continue
        candidates = incoming.get((parent, v), [])
        if not candidates:
            predecessor_errors.append(v)
        else:
            # Pick the closest real input edge; exact ties have priority.
            predecessor_edges[v] = min(candidates, key=lambda e: (abs(distances[parent]+e['w']-distances[v]), e['id']))['id']

    color = [0]*n
    color[source] = 2
    cycle_vertices = []
    for v in range(n):
        if expected[v] is None or color[v]: continue
        chain, u = [], v
        while isinstance(u, int) and not isinstance(u, bool) and 0 <= u < n and color[u] == 0:
            color[u] = 1; chain.append(u); u = predecessors[u]
        if isinstance(u, int) and not isinstance(u, bool) and 0 <= u < n and color[u] == 1:
            cycle_vertices.append(u)
        elif not (isinstance(u, int) and not isinstance(u, bool) and 0 <= u < n and color[u] == 2):
            predecessor_errors.append(v)
        for item in chain: color[item] = 2
    result['predecessor_edges'] = predecessor_edges
    exact_path_bad, tolerance_path_bad = [], []
    structure_ok = not predecessor_errors and not cycle_vertices
    if structure_ok:
        children = [[] for _ in range(n)]
        by_id = {edge['id']: edge['w'] for edge in graph['edges']}
        for v, parent in enumerate(predecessors):
            if parent is not None: children[parent].append(v)
        costs = [None]*n; costs[source] = 0.0
        stack = [source]
        while stack:
            u = stack.pop()
            for v in children[u]:
                costs[v] = costs[u] + by_id[predecessor_edges[v]]
                stack.append(v)
        for v, want in enumerate(expected):
            if want is None: continue
            cost, reported = costs[v], distances[v]
            if (cost is None or not finite_number(reported) or cost != reported or cost != want):
                exact_path_bad.append(v)
            if (cost is None or not finite_number(reported)
                    or not math.isclose(cost, reported, rel_tol=1e-9, abs_tol=1e-9)
                    or not math.isclose(cost, want, rel_tol=1e-9, abs_tol=1e-9)):
                tolerance_path_bad.append(v)
    distance_exact_ok = not exact_bad
    distance_tolerance_ok = not tolerance_bad
    path_exact_ok = structure_ok and not exact_path_bad
    path_tolerance_ok = structure_ok and not tolerance_path_bad
    return {'ok': (distance_exact_ok and path_exact_ok) if mode == 'exact' else (distance_tolerance_ok and path_tolerance_ok),
            'oracle': 'Independent Dijkstra (Python heapq)', 'comparison_mode': mode,
            'integer_exact_domain': integer_exact,
            'distance_exact_ok': distance_exact_ok, 'distance_tolerance_ok': distance_tolerance_ok,
            'path_exact_ok': path_exact_ok, 'path_tolerance_ok': path_tolerance_ok,
            'wrong_vertices': len(exact_bad if mode == 'exact' else tolerance_bad),
            'first_wrong_vertices': (exact_bad if mode == 'exact' else tolerance_bad)[:10],
            'predecessor_errors': predecessor_errors[:10], 'cycle_vertices': cycle_vertices[:10],
            'path_cost_errors': (exact_path_bad if mode == 'exact' else tolerance_path_bad)[:10],
            'path_exact_errors': exact_path_bad[:10], 'path_tolerance_errors': tolerance_path_bad[:10],
            'exact_mismatches': len(exact_bad), 'max_abs_error': max_abs_error,
            'rtol': 1e-9, 'atol': 1e-9}


def demo_graph():
    edges = [[0,1,4],[0,5,2],[0,6,9],[1,2,3],[1,6,2],[2,3,2],
             [2,7,4],[3,4,3],[3,8,2],[4,9,1],[5,6,1],[5,10,5],
             [6,7,2],[6,11,3],[7,8,1],[7,12,4],[8,9,2],[8,13,3],
             [9,14,2],[10,11,1],[10,15,3],[11,12,0],[11,6,1],
             [12,13,2],[12,16,5],[13,14,1],[14,16,2],[15,16,2],
             [0,1,7],[7,12,6]]
    positions = [{'x':100+(i%5)*190,'y':95+(i//5)*170} for i in range(17)]
    positions.append({'x':880,'y':605})
    return normalize_graph({'id':'demo','name':'Mạng minh họa · 18 đỉnh','n':18,
                            'source':0,'target':16,'edges':edges,'positions':positions,
                            'metadata':{'description':'Có cạnh 0, cạnh song song, chu trình và đỉnh 17 không tới được.'}})


def weighted_graph_100():
    """Seeded, sparse directed grid with cycles and local shortcuts.

    The right/down backbone guarantees all 100 vertices are reachable from 0.
    The same persisted input and source are used by both native algorithms.
    """
    rng=random.Random(20261114)
    pairs=set()
    edges=[]
    def add(u,v):
        if (u,v) not in pairs:
            pairs.add((u,v))
            edges.append([u,v,rng.randint(1,20)])
    for row in range(10):
        for col in range(10):
            u=row*10+col
            if col<9:
                add(u,u+1)
            if row<9:
                add(u,u+10)
    # Bidirectional local links produce cycles; diagonal links offer alternative paths.
    reverse=[(v,u) for u,v in sorted(pairs)]
    rng.shuffle(reverse)
    for u,v in reverse[:30]:
        add(u,v)
    diagonals=[(r*10+c,(r+1)*10+c+1) for r in range(9) for c in range(9)]
    rng.shuffle(diagonals)
    for u,v in diagonals[:30]:
        add(u,v)
    positions=[{'x':60+(i%10)*98,'y':50+(i//10)*65} for i in range(100)]
    return normalize_graph({'id':'weighted_100','name':'Đồ thị chung · 100 đỉnh có trọng số',
                            'n':100,'source':0,'target':99,'edges':edges,'positions':positions,
                            'metadata':{'seed':20261114,'kind':'weighted_grid_100',
                                        'description':'100 đỉnh, 240 cạnh có hướng; trọng số nguyên 1–20. Cùng đầu vào cho Dijkstra và BMSSP.'}})
