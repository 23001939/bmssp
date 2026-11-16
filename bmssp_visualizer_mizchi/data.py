"""Validated graph interchange, layout and independent Dijkstra verification for Mizchi Demo."""
from __future__ import annotations

import heapq
import json
import math
import random
from typing import Any, Dict, List, Optional, Tuple

MAX_N = 500_000
MAX_EDGES = 1_500_000


def integer(value: Any, label: str, minimum: int = 0, maximum: Optional[int] = None) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f'{label} phải là số nguyên.')
    if value < minimum or (maximum is not None and value > maximum):
        raise ValueError(f'{label} ngoài giới hạn cho phép ({minimum}–{maximum}).')
    return value


def finite(value: Any, label: str, minimum: Optional[float] = None, maximum: Optional[float] = None) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f'{label} phải là số hữu hạn.')
    try:
        number = float(value)
    except (OverflowError, ValueError):
        raise ValueError(f'{label} phải là số hữu hạn.') from None
    if not math.isfinite(number):
        raise ValueError(f'{label} phải là số hữu hạn.')
    if minimum is not None and number < minimum:
        raise ValueError(f'{label} phải >= {minimum}.')
    if maximum is not None and number > maximum:
        raise ValueError(f'{label} phải <= {maximum}.')
    return number


def layout(n: int) -> List[Dict[str, float]]:
    if n == 1:
        return [{'x': 500.0, 'y': 350.0}]
    if n <= 80:
        cols = max(2, math.ceil(math.sqrt(n * 1.6)))
        rows = math.ceil(n / cols)
        return [{'x': 90.0 + (i % cols) * 820.0 / max(1, cols - 1),
                 'y': 75.0 + (i // cols) * 550.0 / max(1, rows - 1)} for i in range(n)]
    return [{'x': 80.0 + ((i * 0.61803398875) % 1) * 840.0,
             'y': 70.0 + ((i * 0.41421356237) % 1) * 560.0} for i in range(n)]



def normalize_graph(raw: Dict[str, Any], name: Optional[str] = None) -> Dict[str, Any]:
    if not isinstance(raw, dict):
        raise ValueError('Đồ thị JSON phải là một object.')
    if 'graph' in raw and isinstance(raw['graph'], dict):
        raw = raw['graph']
    n = integer(raw.get('n'), 'Số đỉnh', 1, MAX_N)
    source = integer(raw.get('source', 0), 'Đỉnh nguồn', 0, n - 1)
    target = raw.get('target', n - 1)
    if target is not None:
        target = integer(target, 'Đỉnh đích', 0, n - 1)
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
        integer(u, f'Đầu cạnh {i}', 0, n - 1)
        integer(v, f'Cuối cạnh {i}', 0, n - 1)
        integer(eid, f'ID cạnh {i}', 0, 2**53 - 1)
        if eid in seen:
            raise ValueError(f'ID cạnh {eid} bị trùng.')
        seen.add(eid)
        weight = finite(w, f'Trọng số cạnh {i}', minimum=0.0)
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
            x = finite(x, 'Tọa độ x', -1e7, 1e7)
            y = finite(y, 'Tọa độ y', -1e7, 1e7)
            checked.append({'x': x, 'y': y})
        positions = checked
    metadata = dict(raw.get('metadata', {})) if isinstance(raw.get('metadata', {}), dict) else {}
    for key in ('seed', 'kind'):
        if key in raw:
            metadata[key] = raw[key]
    return {
        'id': str(raw.get('id', 'custom'))[:100],
        'name': str(name or raw.get('name') or 'Đồ thị đã nhập')[:160],
        'n': n,
        'source': source,
        'target': target,
        'edges': edges,
        'positions': positions,
        'metadata': metadata,
    }


def parse_text(text: str, fmt: str = 'txt', name: Optional[str] = None) -> Dict[str, Any]:
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
        if len(head) < 2:
            raise ValueError('Dòng đầu cần n m.')
        n, m = int(head[0]), int(head[1])
        if m < 0 or m > MAX_EDGES or len(lines) - 1 != m:
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


def dijkstra(graph: Dict[str, Any], source: int) -> Tuple[List[Optional[float]], List[Optional[int]], List[Optional[int]]]:
    n = graph['n']
    integer(source, 'Đỉnh nguồn', 0, n - 1)
    adj: List[List[Tuple[int, float, int]]] = [[] for _ in range(n)]
    for edge in graph['edges']:
        adj[edge['u']].append((edge['v'], edge['w'], edge['id']))
    dist = [math.inf] * n
    pred: List[Optional[int]] = [None] * n
    pe: List[Optional[int]] = [None] * n
    dist[source] = 0.0
    heap: List[Tuple[float, int]] = [(0.0, source)]
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


def verify_result(graph: Dict[str, Any], source: int, result: Dict[str, Any], comparison_mode: str = 'auto') -> Dict[str, Any]:
    """Verify distances and optional source-rooted paths in binary64 arithmetic."""
    if comparison_mode not in ('auto', 'exact', 'tolerance'):
        raise ValueError('Comparison mode must be auto, exact or tolerance.')
    expected_dist, oracle_pred, oracle_pe = dijkstra(graph, source)
    distances = result.get('distances', [])
    predecessors = result.get('predecessors')
    n = graph['n']
    weights = [edge['w'] for edge in graph['edges']]
    integer_exact = (all(float(w).is_integer() for w in weights)
                     and int(max(weights, default=0)) * max(0, n - 1) <= 2**53)
    mode = ('exact' if integer_exact else 'tolerance') if comparison_mode == 'auto' else comparison_mode

    if not isinstance(distances, list) or len(distances) != n:
        return {
            'ok': False,
            'error': f'Số lượng khoảng cách ({len(distances) if isinstance(distances, list) else type(distances)}) không khớp n={n}.',
            'wrong_vertices': n,
            'comparison_mode': mode,
            'distance_exact_ok': False,
            'distance_tolerance_ok': False,
        }

    def finite_number(x: Any) -> bool:
        return isinstance(x, (int, float)) and not isinstance(x, bool) and math.isfinite(x)

    exact_bad, tolerance_bad = [], []
    max_abs_error = 0.0
    for v, (actual, want) in enumerate(zip(distances, expected_dist)):
        equal = (actual is None) if want is None else (finite_number(actual) and actual == want)
        close = (actual is None) if want is None else (finite_number(actual) and math.isclose(actual, want, rel_tol=1e-9, abs_tol=1e-9))
        if not equal:
            exact_bad.append(v)
        if not close:
            tolerance_bad.append(v)
        if want is not None and finite_number(actual):
            max_abs_error = max(max_abs_error, abs(actual - want))

    distance_exact_ok = not exact_bad
    distance_tolerance_ok = not tolerance_bad
    dist_ok = distance_exact_ok if mode == 'exact' else distance_tolerance_ok

    wrong_count = len(exact_bad if mode == 'exact' else tolerance_bad)
    return {
        'ok': dist_ok,
        'oracle': 'Independent Dijkstra (Python heapq)',
        'comparison_mode': mode,
        'integer_exact_domain': integer_exact,
        'distance_exact_ok': distance_exact_ok,
        'distance_tolerance_ok': distance_tolerance_ok,
        'wrong_vertices': wrong_count,
        'first_wrong_vertices': (exact_bad if mode == 'exact' else tolerance_bad)[:10],
        'exact_mismatches': len(exact_bad),
        'max_abs_error': max_abs_error,
        'oracle_distances': expected_dist,
    }

def demo_graph() -> Dict[str, Any]:
    edges = [
        [0, 1, 4.0], [0, 5, 2.0], [0, 6, 9.0], [1, 2, 3.0], [1, 6, 2.0], [2, 3, 2.0],
        [2, 7, 4.0], [3, 4, 3.0], [3, 8, 2.0], [4, 9, 1.0], [5, 6, 1.0], [5, 10, 5.0],
        [6, 7, 2.0], [6, 11, 3.0], [7, 8, 1.0], [7, 12, 4.0], [8, 9, 2.0], [8, 13, 3.0],
        [9, 14, 2.0], [10, 11, 1.0], [10, 15, 3.0], [11, 12, 0.0], [11, 6, 1.0],
        [12, 13, 2.0], [12, 16, 5.0], [13, 14, 1.0], [14, 16, 2.0], [15, 16, 2.0],
        [0, 1, 7.0], [7, 12, 6.0],
    ]
    positions = [{'x': 100.0 + (i % 5) * 190.0, 'y': 95.0 + (i // 5) * 170.0} for i in range(17)]
    positions.append({'x': 880.0, 'y': 605.0})
    return normalize_graph({
        'id': 'demo',
        'name': 'Mạng minh họa · 18 đỉnh (mizchi)',
        'n': 18,
        'source': 0,
        'target': 16,
        'edges': edges,
        'positions': positions,
        'metadata': {
            'description': 'Đồ thị minh họa 18 đỉnh: có trọng số 0, cạnh song song, chu trình và đỉnh 17 không tới được.',
        },
    })


def weighted_graph_50() -> Dict[str, Any]:
    """Seeded, sparse directed grid with cycles and local shortcuts. 50 vertices, ~120 edges, guaranteed reachability."""
    rng = random.Random(20261115)
    pairs = set()
    edges = []

    def add(u: int, v: int) -> None:
        if (u, v) not in pairs:
            pairs.add((u, v))
            edges.append([u, v, float(rng.randint(1, 15))])

    # 5 rows x 10 cols grid spanning edges
    for row in range(5):
        for col in range(10):
            u = row * 10 + col
            if col < 9:
                add(u, u + 1)
            if row < 4:
                add(u, u + 10)

    # 15 reverse edges for cycles
    reverse = [(v, u) for u, v in sorted(pairs)]
    rng.shuffle(reverse)
    for u, v in reverse[:15]:
        add(u, v)

    # 20 diagonal shortcuts
    diagonals = [(r * 10 + c, (r + 1) * 10 + c + 1) for r in range(4) for c in range(9)]
    rng.shuffle(diagonals)
    for u, v in diagonals[:20]:
        add(u, v)

    positions = [{'x': 80.0 + (i % 10) * 94.0, 'y': 75.0 + (i // 10) * 125.0} for i in range(50)]
    return normalize_graph({
        'id': 'weighted_50',
        'name': 'Đồ thị lưới trung bình · 50 đỉnh có trọng số',
        'n': 50,
        'source': 0,
        'target': 49,
        'edges': edges,
        'positions': positions,
        'metadata': {
            'seed': 20261115,
            'kind': 'weighted_grid_50',
            'description': '50 đỉnh, 120 cạnh có hướng, trọng số nguyên 1–15. Phù hợp quan sát biến đổi bậc và so sánh 3 thuật toán.',
        },
    })


def weighted_graph_100() -> Dict[str, Any]:
    """Seeded, sparse directed grid with cycles and local shortcuts. Guaranteed reachability."""
    rng = random.Random(20261114)
    pairs = set()
    edges = []

    def add(u: int, v: int) -> None:
        if (u, v) not in pairs:
            pairs.add((u, v))
            edges.append([u, v, float(rng.randint(1, 20))])

    for row in range(10):
        for col in range(10):
            u = row * 10 + col
            if col < 9:
                add(u, u + 1)
            if row < 9:
                add(u, u + 10)
    reverse = [(v, u) for u, v in sorted(pairs)]
    rng.shuffle(reverse)
    for u, v in reverse[:30]:
        add(u, v)
    diagonals = [(r * 10 + c, (r + 1) * 10 + c + 1) for r in range(9) for c in range(9)]
    rng.shuffle(diagonals)
    for u, v in diagonals[:30]:
        add(u, v)
    positions = [{'x': 60.0 + (i % 10) * 98.0, 'y': 50.0 + (i // 10) * 65.0} for i in range(100)]
    return normalize_graph({
        'id': 'weighted_100',
        'name': 'Đồ thị chung · 100 đỉnh có trọng số',
        'n': 100,
        'source': 0,
        'target': 99,
        'edges': edges,
        'positions': positions,
        'metadata': {
            'seed': 20261114,
            'kind': 'weighted_grid_100',
            'description': '100 đỉnh, 240 cạnh có hướng, trọng số nguyên 1–20. Dùng so sánh DMM+25, C-HD và Dijkstra.',
        },
    })


def graph_to_txt(graph: Dict[str, Any]) -> str:
    """Format graph into TXT: first line 'n m', then m lines 'u v w'."""
    lines = [f"{graph['n']} {len(graph['edges'])}"]
    for e in graph['edges']:
        w = e['w']
        w_str = str(int(w)) if isinstance(w, (int, float)) and float(w).is_integer() else str(w)
        lines.append(f"{e['u']} {e['v']} {w_str}")
    return '\n'.join(lines) + '\n'


