"""Run scoped checks and record PASS/FAIL/SKIPPED without implied acceptance.

Default: verify without opening a browser. --quick checks C++ on ten random inputs;
full checks C++ and Rust on twenty. --launch-demo explicitly opens the demo after
successful checks; --demo-only launches it and marks verification SKIPPED.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import json
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent
VISUALIZER_DIR = ROOT/'bmssp_visualizer'
COMP_DIR = ROOT/'bmssp_comparison'
SCHEMA = 'bmssp.verification-manifest.v1'


def process_step(name, command, timeout=120, required=True):
    started = time.perf_counter()
    result = {'name': name, 'required': required, 'command': list(map(str, command)), 'cwd': str(ROOT)}
    try:
        child = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                               encoding='utf-8', errors='replace', timeout=timeout)
        result.update(status='PASS' if child.returncode == 0 else 'FAIL', exit_code=child.returncode,
                      stdout=child.stdout, stderr=child.stderr)
    except (OSError, subprocess.TimeoutExpired) as error:
        result.update(status='FAIL', exit_code=None, error=str(error),
                      error_kind='timeout' if isinstance(error, subprocess.TimeoutExpired) else 'missing_tool_or_os_error')
    result['elapsed_seconds'] = time.perf_counter()-started
    return result


def skipped(name, reason, required=False):
    return {'name': name, 'status': 'SKIPPED', 'required': required, 'exit_code': None, 'reason': reason}


def unittest_step(name, directory, timeout):
    if not directory.is_dir() or not list(directory.glob('test_*.py')):
        return {'name': name, 'required': True, 'status': 'FAIL', 'exit_code': None,
                'error': 'Required test scripts are missing.'}
    result = process_step(name, [sys.executable, '-X', 'utf8', '-m', 'unittest',
                                'discover', '-s', str(directory), '-p', 'test_*.py'], timeout=timeout)
    if result['status'] == 'PASS':
        match = re.search(r'Ran (\d+) tests?\b', result.get('stdout', '')+'\n'+result.get('stderr', ''))
        if not match or int(match.group(1)) == 0:
            result.update(status='FAIL', error='Unittest returned zero without evidence of executed tests.')
        else:
            result['tests_run'] = int(match.group(1))
    return result


def fidelity_step(args, output):
    script = COMP_DIR/'run_fidelity_checks.py'
    if not script.is_file():
        return {'name': 'empirical_distance_checks', 'status': 'FAIL', 'required': True,
                'exit_code': None, 'error': 'Required run_fidelity_checks.py is missing.'}
    output.unlink(missing_ok=True)
    limit = args.limit if args.limit is not None else (10 if args.quick else 20)
    solvers = ['cpp'] if args.quick else ['cpp', 'rust']
    command = [sys.executable, '-X', 'utf8', str(script), '--limit', str(limit), '--seed', str(args.seed),
               '--timeout', str(args.timeout), '--output', str(output), '--solvers', *solvers]
    result = process_step('empirical_distance_checks', command, timeout=args.timeout*max(2, limit*len(solvers))+30)
    try:
        payload = json.loads(output.read_text(encoding='utf-8'))
        if payload.get('schema') != 'bmssp.empirical-distance-checks.v1':
            raise ValueError('Unexpected fidelity manifest schema')
        config = payload.get('config', {})
        if config.get('seed') != args.seed or config.get('limit') != limit or config.get('solvers') != solvers:
            raise ValueError('Child manifest does not match requested seed/limit/solvers')
        cases = payload.get('cases', [])
        if payload.get('completed_cases') != limit or len(cases) != limit:
            raise ValueError('Child did not execute the requested number of random inputs')
        if any(set(case.get('results', {})) != set(solvers) for case in cases):
            raise ValueError('Missing requested solver results')
        results = [r for case in cases for r in case.get('results', {}).values()]
        if (payload.get('status') != 'PASS' or payload.get('accuracy') is not True or payload.get('exit_code') != 0
                or not results or any(r.get('status') != 'PASS' or r.get('accuracy') is not True for r in results)):
            raise ValueError('Child manifest reports failed, skipped or inaccurate required results')
        result['evidence'] = {'manifest': str(output), 'status': payload['status'], 'completed_cases': limit,
                              'diagnostic_failures': payload.get('diagnostic_failures'), 'scope': payload.get('scope'),
                              'provenance': payload.get('provenance')}
    except (OSError, ValueError, TypeError, AttributeError) as error:
        result.update(status='FAIL', manifest_error=str(error))
    return result


def read_history(path):
    step = skipped('historical_artifact', 'Historical evidence only; not executed by this command.')
    step['path'] = str(path.resolve())
    try:
        payload = json.loads(path.read_text(encoding='utf-8'))
        if not isinstance(payload, dict) or not payload.get('schema') or not payload.get('config') or not payload.get('provenance'):
            raise ValueError('Historical file lacks structured schema, configuration or provenance')
        step['historical_evidence'] = {key: payload.get(key) for key in ('schema', 'status', 'accuracy', 'config', 'provenance')}
    except (OSError, ValueError) as error:
        step['history_error'] = str(error)
    return step


def summarize(steps, verification_requested):
    failed = any(step['required'] and step['status'] != 'PASS' for step in steps)
    return ('FAIL' if failed else ('PASS' if verification_requested else 'SKIPPED')), (1 if failed else 0)


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    scope = parser.add_mutually_exclusive_group()
    scope.add_argument('--quick', action='store_true', help='Production regressions + C++ distances; default ten random inputs, no Rust check')
    scope.add_argument('--demo-only', action='store_true', help='Launch demo; all verification is explicitly SKIPPED')
    parser.add_argument('--launch-demo', action='store_true', help='Launch only after requested checks pass')
    parser.add_argument('--no-browser', action='store_true', help='Do not open a browser when launching the demo')
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--dataset', default='weighted_100')
    parser.add_argument('--seed', type=int, default=20261114)
    parser.add_argument('--limit', type=int, help='Required random fidelity inputs; overrides quick/full default')
    parser.add_argument('--timeout', type=int, default=120, help='Child timeout in seconds; fidelity applies it per solver process')
    parser.add_argument('--output', type=Path, default=COMP_DIR/'remediation/verification_manifest.json')
    parser.add_argument('--history', type=Path, help='Inspect historical evidence without treating it as a fresh check')
    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    if (args.limit is not None and args.limit < 1) or args.timeout < 1 or not 1 <= args.port <= 65535:
        parser.error('limit/timeout must be positive; port must be 1..65535')
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    steps = []
    if args.demo_only:
        steps.extend(skipped(name, 'demo-only: verification was not run.') for name in
                     ['python', 'cpp_compiler', 'production_regressions', 'native_regressions', 'empirical_distance_checks'])
    else:
        steps.append(process_step('python', [sys.executable, '--version'], timeout=10))
        steps.append(process_step('cpp_compiler', ['g++', '--version'], timeout=10))
        if any(step['status'] == 'FAIL' for step in steps):
            steps.extend(skipped(name, 'Required toolchain preflight failed.', required=True)
                         for name in ['production_regressions', 'native_regressions', 'empirical_distance_checks'])
        else:
            steps.append(unittest_step('production_regressions', VISUALIZER_DIR/'tests', args.timeout))
            steps.append(unittest_step('native_regressions', VISUALIZER_DIR/'native', args.timeout))
            steps.append(fidelity_step(args, output.with_name(output.stem+'_fidelity.json')))
        if args.quick:
            steps.append(skipped('rust_checks', 'quick: Rust is outside the requested scope.'))
    if args.history:
        steps.append(read_history(args.history))
    status, exit_code = summarize(steps, not args.demo_only)
    if args.demo_only or args.launch_demo:
        script = VISUALIZER_DIR/'start_demo.py'
        if exit_code:
            steps.append(skipped('launch_demo', 'Required verification failed; demo was not launched.'))
        elif not script.is_file():
            steps.append({'name': 'launch_demo', 'required': True, 'status': 'FAIL', 'exit_code': None,
                          'error': 'start_demo.py is missing.'})
        else:
            command = [sys.executable, '-X', 'utf8', str(script), '--port', str(args.port), '--dataset', args.dataset]
            if args.no_browser:
                command.append('--no-browser')
            steps.append(process_step('launch_demo', command, timeout=args.timeout))
        status, exit_code = summarize(steps, not args.demo_only)
    else:
        steps.append(skipped('launch_demo', 'Use --launch-demo or --demo-only to start the visualizer.'))
    manifest = {'schema': SCHEMA, 'created_utc': datetime.now(timezone.utc).isoformat(),
                'status': status, 'exit_code': exit_code,
                'verification_scope': 'none (demo-only)' if args.demo_only else ('quick: production regressions + C++ distances' if args.quick else 'production regressions + C++/Rust distances'),
                'config': {'quick': args.quick, 'demo_only': args.demo_only, 'seed': args.seed,
                           'limit': args.limit if args.limit is not None else (10 if args.quick else 20),
                           'timeout_seconds': args.timeout}, 'steps': steps,
                'provenance': {'launcher_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                               'python': sys.version, 'root': str(ROOT),
                               'empirical_checks': next((step.get('evidence', {}).get('provenance') for step in steps
                                                         if step['name'] == 'empirical_distance_checks'), None)},
                'limitations': ['Finite empirical checks do not certify Algorithms 1-3/Lemma 3.3.',
                                'No benchmark or transform remeasurement is performed by this command.',
                                'Historical files never count as a fresh PASS.']}
    output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, allow_nan=False), encoding='utf-8')
    for step in steps:
        reason = step.get('error') or step.get('reason') or step.get('manifest_error')
        print(f"{step['status']:7} {step['name']}"+(f' - {reason}' if reason else ''))
    print(f'{status}: {manifest["verification_scope"]}; manifest: {output}')
    return exit_code


if __name__ == '__main__':
    raise SystemExit(main())
