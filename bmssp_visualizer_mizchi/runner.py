"""Python boundary for runners: invoke Rust batch or traced adapters and validate results."""
from __future__ import annotations

import json
from pathlib import Path
import subprocess
from typing import Any, Dict

try:
    from build_rust import ensure_built, ensure_traced_built
except ImportError:
    from bmssp_visualizer_mizchi.build_rust import ensure_built, ensure_traced_built

ALGORITHMS = frozenset({'dmm25', 'chd', 'dijkstra'})


def validate_native_result(result: Dict[str, Any], source: int, algorithm: str, expected_n: int) -> Dict[str, Any]:
    if not isinstance(result, dict) or result.get('type') != 'native_result':
        raise RuntimeError('Rust runner thiếu type native_result.')
    if result.get('algorithm_id') != algorithm and result.get('algorithm') != algorithm:
        raise RuntimeError('Rust runner trả algorithm không khớp yêu cầu.')
    if result.get('source') != source:
        raise RuntimeError('Rust runner trả source không khớp yêu cầu.')
    n = result.get('n')
    distances = result.get('distances')
    if isinstance(n, bool) or not isinstance(n, int) or n != expected_n or not isinstance(distances, list) or len(distances) != n:
        raise RuntimeError('Rust runner trả distances không khớp n.')
    if any(value is not None and (isinstance(value, bool) or not isinstance(value, (int, float))) for value in distances):
        raise RuntimeError('Rust runner trả một distance không phải number/null.')
    if not isinstance(result.get('stats'), dict):
        raise RuntimeError('Rust runner thiếu stats object.')
    return result


def run_batch(input_path: Path, source: int, algorithm: str) -> dict:
    """Run one solver on TXT input in batch mode and return validated native_result."""
    if algorithm not in ALGORITHMS:
        raise ValueError(f'algorithm phải thuộc {sorted(ALGORITHMS)}.')
    if isinstance(source, bool) or not isinstance(source, int) or source < 0:
        raise ValueError('source phải là số nguyên không âm.')
    path = Path(input_path).resolve()
    if not path.is_file():
        raise ValueError(f'Không tìm thấy input TXT: {path}')
    completed = subprocess.run(
        [str(ensure_built()), str(path), str(source), algorithm],
        cwd=Path(__file__).resolve().parent,
        capture_output=True,
        text=True,
        encoding='utf-8',
        check=False,
    )
    if completed.returncode:
        detail = completed.stderr.strip() or completed.stdout.strip() or f'mã {completed.returncode}'
        raise RuntimeError(f'Rust batch_runner thất bại: {detail}')
    lines = [line for line in completed.stdout.splitlines() if line.strip()]
    if len(lines) != 1:
        raise RuntimeError('Rust batch_runner phải trả đúng một dòng JSON.')
    try:
        result = json.loads(lines[0])
    except json.JSONDecodeError as error:
        raise RuntimeError(f'Rust batch_runner trả JSON không hợp lệ: {error}') from None
    n = result.get('n', 0)
    return validate_native_result(result, source, algorithm, n)


def launch_traced_process(input_path: Path, source: int, algorithm: str, mode: str = 'trace', sample_every: int = 1) -> subprocess.Popen:
    """Launch traced_runner subprocess with stdio piped for streaming and control."""
    if algorithm not in ALGORITHMS:
        raise ValueError(f'algorithm phải thuộc {sorted(ALGORITHMS)}.')
    if isinstance(source, bool) or not isinstance(source, int) or source < 0:
        raise ValueError('source phải là số nguyên không âm.')
    if mode not in ('trace', 'plain'):
        raise ValueError('mode phải là trace hoặc plain.')
    if sample_every < 1:
        raise ValueError('sample_every phải >= 1.')
    path = Path(input_path).resolve()
    if not path.is_file():
        raise ValueError(f'Không tìm thấy input TXT: {path}')
    exe = ensure_traced_built()
    return subprocess.Popen(
        [str(exe), str(path), str(source), algorithm, mode, str(sample_every)],
        cwd=Path(__file__).resolve().parent,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding='utf-8',
        bufsize=1,
    )
