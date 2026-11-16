"""Reproducible empirical distance checks, not certification of BMSSP invariants."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import random
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
SUFFIX = '.exe' if os.name == 'nt' else ''
CPP_BIN = ROOT/'bmssp_comparison'/('bench_cpp'+SUFFIX)
RUST_BIN = ROOT/'bmssp_comparison/repos/rust_python/target/release/examples'/('bench_rust'+SUFFIX)
SCHEMA = 'bmssp.empirical-distance-checks.v1'


def file_hash(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest() if Path(path).is_file() else None


def command_info(command, cwd=ROOT, timeout=10):
    started = time.perf_counter()
    result = {'command': list(map(str, command)), 'cwd': str(cwd)}
    try:
        child = subprocess.run(command, cwd=cwd, capture_output=True, text=True,
                               encoding='utf-8', errors='replace', timeout=timeout)
        result.update(status='PASS' if child.returncode == 0 else 'FAIL', exit_code=child.returncode,
                      stdout=child.stdout, stderr=child.stderr)
    except (OSError, subprocess.TimeoutExpired) as error:
        result.update(status='FAIL', exit_code=None, error=str(error),
                      error_kind='timeout' if isinstance(error, subprocess.TimeoutExpired) else 'missing_tool_or_os_error')
    result['elapsed_seconds'] = time.perf_counter()-started
    return result


def provenance():
    repos = {}
    for name, directory in [('project', ROOT), ('cpp', ROOT/'bmssp_comparison/repos/cpp'),
                            ('rust', ROOT/'bmssp_comparison/repos/rust_python')]:
        commit = command_info(['git', '-C', str(directory), 'rev-parse', 'HEAD'])
        diff = command_info(['git', '-C', str(directory), 'diff', '--no-ext-diff', 'HEAD'])
        status = command_info(['git', '-C', str(directory), 'status', '--porcelain'])
        repos[name] = {'commit': commit.get('stdout', '').strip() if commit['status'] == 'PASS' else None,
                       'commit_error': commit.get('error') or commit.get('stderr'),
                       'diff_sha256': hashlib.sha256(diff.get('stdout', '').encode()).hexdigest() if diff['status'] == 'PASS' else None,
                       'diff_status': diff['status'], 'working_tree_status': status.get('stdout')}
    return {'python': sys.version, 'platform': sys.platform, 'repos': repos,
            'compiler': command_info(['g++', '--version']), 'rustc': command_info(['rustc', '--version']),
            'script_sha256': file_hash(__file__), 'wrapper_sha256': file_hash(ROOT/'bmssp_comparison/bench_cpp.cpp')}


def generate_random_graph(n, m, seed, weight_type='real'):
    rng = random.Random(seed)
    def weight():
        if weight_type == 'integer':
            return rng.randint(1, 20)
        if weight_type == 'zero_ties':
            return rng.choice([0, 1, 2])
        return round(rng.uniform(.1, 10), 4)
    edges = [(i, i+1, weight()) for i in range(n-1)]
    edges.extend((rng.randrange(n), rng.randrange(n), weight()) for _ in range(max(0, m-len(edges))))
    return edges


def make_cases(limit, seed):
    """Seed controls n, m, graph family and every edge/weight choice."""
    rng = random.Random(seed)
    cases = []
    for index in range(limit):
        n = rng.randint(50, 400)
        m = rng.randint(n, 4*n)
        kind = rng.choice(['real', 'integer', 'zero_ties'])
        graph_seed = rng.getrandbits(63)
        cases.append({'name': f'random_{index:04d}', 'n': n, 'm': m, 'source': 0,
                      'seed': graph_seed, 'weight_type': kind, 'required': True,
                      'arithmetic': 'tolerance' if kind == 'real' else 'exact',
                      'edges': generate_random_graph(n, m, graph_seed, kind)})
    return cases


def graph_text(case):
    return '\n'.join([f"{case['n']} {len(case['edges'])}"] +
                     [f'{u} {v} {w:.17g}' for u, v, w in case['edges']])+'\n'


def bellman_ford(case):
    """Independent strict relaxations; integer cases use Python exact integers."""
    distances = [math.inf]*case['n']
    distances[case['source']] = 0
    for _ in range(case['n']-1):
        changed = False
        for u, v, weight in case['edges']:
            candidate = distances[u]+weight
            if candidate < distances[v]:
                distances[v] = candidate
                changed = True
        if not changed:
            break
    return distances


def compare_distances(expected, actual, arithmetic, tolerance=1e-9):
    if len(expected) != len(actual):
        return {'accuracy': False, 'error': 'wrong_distance_count', 'expected': len(expected), 'actual': len(actual)}
    bad, exact, max_error = [], [], 0.0
    for vertex, (want, got) in enumerate(zip(expected, actual)):
        if math.isnan(got) or got == -math.inf:
            bad.append(vertex)
            exact.append(vertex)
            continue
        equal = want == got
        if not equal:
            exact.append(vertex)
        if math.isinf(want) or math.isinf(got):
            accepted = equal
        else:
            error = abs(want-got)
            max_error = max(max_error, error)
            accepted = equal if arithmetic == 'exact' else error <= tolerance*max(1, abs(want))
        if not accepted:
            bad.append(vertex)
    return {'accuracy': not bad, 'mismatches': len(bad), 'mismatch_vertices': bad[:30],
            'exact_mismatches': len(exact), 'max_abs_error': max_error,
            'arithmetic': arithmetic, 'relative_tolerance': tolerance if arithmetic == 'tolerance' else 0}


def run_solver(exe, mode, input_path, output_path, n, timeout):
    output_path = Path(output_path)
    output_path.unlink(missing_ok=True)
    result = command_info([str(exe), mode, str(input_path), str(output_path)], timeout=timeout)
    if result['status'] != 'PASS':
        return result, None
    try:
        payload = json.loads(result.get('stdout', '').strip().splitlines()[-1])
        if not isinstance(payload, dict) or payload.get('accuracy') is False:
            raise ValueError('Invalid stdout object or child accuracy=false')
        samples = payload.get('samples_ms')
        if not isinstance(samples, list) or not samples or any(not isinstance(x, (int, float)) or not math.isfinite(x) or x < 0 for x in samples):
            raise ValueError('Missing or invalid samples_ms')
        values = [float(line) for line in output_path.read_text(encoding='utf-8').splitlines() if line.strip()]
        if len(values) != n or any(math.isnan(x) or x == -math.inf or x < 0 for x in values):
            raise ValueError(f'Invalid distances: expected {n} finite nonnegative/positive-infinity entries')
        result['child_json'] = payload
        return result, values
    except (ValueError, OSError, IndexError) as error:
        result.update(status='FAIL', error=str(error), error_kind='invalid_child_result')
        return result, None


def execute(args):
    output = args.output.resolve()
    input_dir = output.parent/(output.stem+'_inputs')
    input_dir.mkdir(parents=True, exist_ok=True)
    selected = {name: (args.cpp_bin, 'wc') if name == 'cpp' else (args.rust_bin, 'bmssp') for name in args.solvers}
    manifest = {'schema': SCHEMA, 'status': 'PASS', 'accuracy': True, 'exit_code': 0,
                'scope': 'Required random distance comparisons only. No predecessor or internal invariant certification. Pristine near-tie is a separate nonrequired diagnostic.',
                'config': {'seed': args.seed, 'limit': args.limit, 'solvers': args.solvers,
                           'timeout_seconds': args.timeout, 'tolerance': 1e-9,
                           'limit_scope': 'required random inputs; fixed diagnostic listed separately'},
                'provenance': provenance(), 'solvers': {}, 'cases': [], 'diagnostics': []}
    for name, (binary, mode) in selected.items():
        exists = Path(binary).is_file()
        manifest['solvers'][name] = {'binary': str(Path(binary).resolve()), 'mode': mode,
                                    'binary_sha256': file_hash(binary), 'status': 'READY' if exists else 'FAIL',
                                    'build_command': None, 'build_flags': None,
                                    'build_provenance': 'Pre-existing executable, not rebuilt here; compiler version now is recorded separately. Original build flags are unverified.',
                                    'error': None if exists else 'Required executable missing; build it before checking.'}
    diagnostic = {'name': 'pristine_near_tie', 'n': 3, 'm': 3, 'source': 0, 'seed': None,
                  'weight_type': 'near_tie', 'arithmetic': 'exact', 'required': False,
                  'edges': [(0, 1, 4e-11), (0, 2, 0), (2, 1, 0)]}
    for case in [diagnostic]+make_cases(args.limit, args.seed):
        text = graph_text(case)
        input_path = input_dir/(case['name']+'.txt')
        input_path.write_text(text, encoding='utf-8')
        record = {key: value for key, value in case.items() if key != 'edges'}
        record.update(input_sha256=hashlib.sha256(text.encode()).hexdigest(), input_path=str(input_path), results={})
        oracle = bellman_ford(case)
        for name, (binary, mode) in selected.items():
            if manifest['solvers'][name]['status'] == 'FAIL':
                result = {'status': 'SKIPPED', 'accuracy': False, 'exit_code': None,
                          'error': 'Required executable missing; solver preflight failed.'}
            else:
                result, distances = run_solver(binary, mode, input_path, input_dir/(case['name']+'_'+name+'.dist'), case['n'], args.timeout)
                if distances is not None:
                    result.update(compare_distances(oracle, distances, case['arithmetic']))
                    result['status'] = 'PASS' if result['accuracy'] else 'FAIL'
                else:
                    result['accuracy'] = False
            record['results'][name] = result
            if case['required'] and result['status'] != 'PASS':
                manifest.update(status='FAIL', accuracy=False, exit_code=1)
        manifest['cases' if case['required'] else 'diagnostics'].append(record)
        print(f"{case['name']} ({'required' if case['required'] else 'diagnostic'}): "+', '.join(f"{name}={r['status']}" for name, r in record['results'].items()), flush=True)
    manifest['completed_cases'] = len(manifest['cases'])
    manifest['passed_solver_cases'] = sum(r['status'] == 'PASS' for case in manifest['cases'] for r in case['results'].values())
    manifest['diagnostic_failures'] = sum(r['status'] == 'FAIL' for case in manifest['diagnostics'] for r in case['results'].values())
    output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, allow_nan=False), encoding='utf-8')
    print(f"{manifest['status']}: {manifest['completed_cases']} required inputs; diagnostics failed: {manifest['diagnostic_failures']}; manifest: {output}")
    return manifest


def positive_int(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError('must be at least 1')
    return number


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--limit', type=positive_int, default=20, help='Number of required random inputs; fixed diagnostic is separate')
    parser.add_argument('--seed', type=int, default=20261114)
    parser.add_argument('--solvers', nargs='+', choices=['cpp', 'rust'], default=['cpp', 'rust'])
    parser.add_argument('--timeout', type=positive_int, default=30, help='Per child process, seconds')
    parser.add_argument('--cpp-bin', type=Path, default=CPP_BIN)
    parser.add_argument('--rust-bin', type=Path, default=RUST_BIN)
    parser.add_argument('--output', type=Path, default=ROOT/'bmssp_comparison/remediation/fidelity_checks.json')
    return parser


def main(argv=None):
    return execute(build_parser().parse_args(argv))['exit_code']


if __name__ == '__main__':
    raise SystemExit(main())
