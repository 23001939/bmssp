#!/usr/bin/env python3
"""Build, verify and time API/prepared SSSP with heap and pure serial scan controls."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent
RUNTIME_PREFIXES = ('OMP_', 'KMP_', 'GOMP_', 'LIBOMP_')


def run(cmd, env=None, timeout=180):
    result = subprocess.run([str(x) for x in cmd], cwd=ROOT, env=env,
                            capture_output=True, text=True, errors='replace', timeout=timeout)
    if result.returncode:
        raise RuntimeError(f'Command failed ({result.returncode}): {cmd}\n{result.stdout}\n{result.stderr}')
    return result.stdout.strip()


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def imported_libraries(exe, compiler_dir, env):
    for name in ('objdump', 'llvm-objdump'):
        local = compiler_dir / (name + ('.exe' if os.name == 'nt' else ''))
        found = str(local) if local.is_file() else shutil.which(name, path=env['PATH'])
        if found:
            result = subprocess.run([found, '-p', str(exe)], cwd=ROOT, env=env,
                                    capture_output=True, text=True, errors='replace')
            if result.returncode == 0:
                return {'tool': found, 'imports': re.findall(r'DLL Name:\s*(\S+)', result.stdout),
                        'output': result.stdout}
    return {'tool': None, 'imports': [], 'note': 'PE import inspection tool unavailable.'}


def main():
    if os.name == 'nt':
        # Scope to this runner and its children, never change machine settings.
        # A crashing probe must return an exit code, not block on a modal dialog.
        import ctypes
        kernel = ctypes.windll.kernel32
        kernel.SetErrorMode(kernel.GetErrorMode() | 0x0001 | 0x0002 | 0x8000)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--output', type=Path, default=ROOT / 'results/dijkstra_openmp_v2.json')
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--affinity', choices=('close', 'none'), default='close')
    args = parser.parse_args()
    compiler = shutil.which(args.cxx)
    if compiler is None:
        parser.error(f'Compiler not found: {args.cxx}')
    compiler = Path(compiler).resolve()
    # Isolate inherited OpenMP settings; persist what was removed for diagnosis.
    removed = {k:v for k,v in os.environ.items() if k.startswith(RUNTIME_PREFIXES)}
    env = {k:v for k,v in os.environ.items() if not k.startswith(RUNTIME_PREFIXES)}
    env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
    env.update(OMP_NUM_THREADS='4', OMP_DYNAMIC='FALSE')
    if args.affinity == 'close':
        env.update(OMP_PROC_BIND='close', OMP_PLACES='cores')
    compiler_version = run([compiler, '--version'], env=env)
    compiler_digest = sha256(compiler)
    identity = hashlib.sha256((str(compiler)+compiler_digest+compiler_version).encode()).hexdigest()[:12]
    build = ROOT / 'build' / ('dijkstra_openmp_v2_' + identity)
    build.mkdir(parents=True, exist_ok=True)
    flags = ['-std=c++20', '-O3', '-mcx16', '-fopenmp', '-Wall', '-Wextra',
             '-Iinclude', '-Iinclude/baseline/external', '-Iinclude/baseline/upstream/include']
    sources = ['src/core/graph.cpp', 'src/baseline/bmssp_seq.cpp', 'src/baseline/dijkstra_openmp.cpp']
    suffix = '.exe' if os.name == 'nt' else ''
    binaries = {}
    compile_logs = {}
    for name, source in [('probe', 'tests/openmp_runtime_probe.cpp'),
                         ('test', 'tests/test_dijkstra_openmp.cpp'),
                         ('bench', 'benchmarks/bench_dijkstra_openmp.cpp')]:
        exe = build / (name + suffix)
        print(f'Building {name}: {exe}', flush=True)
        linked_sources = [] if name == 'probe' else sources
        result = subprocess.run([str(compiler), *flags, *linked_sources, source, '-o', str(exe)],
                                cwd=ROOT, env=env, capture_output=True, text=True,
                                errors='replace', timeout=180)
        compile_logs[name] = {'returncode':result.returncode, 'stdout':result.stdout, 'stderr':result.stderr}
        if result.returncode:
            raise RuntimeError(f'Build {name} failed: {compile_logs[name]}')
        binaries[name] = exe
    if args.build_only:
        print(f'Build only complete: {build}')
        return
    validations = []
    for label, overrides in [('normal', {}), ('limited_team', {'OMP_THREAD_LIMIT':'2'}),
                             ('dynamic_team', {'OMP_DYNAMIC':'TRUE'})]:
        case_env = dict(env, **overrides)
        probe = subprocess.run([str(binaries['probe'])], cwd=ROOT, env=case_env,
                               capture_output=True, text=True, errors='replace', timeout=60)
        probe_record = {'returncode':probe.returncode,
                        'returncode_hex':f'0x{probe.returncode & 0xffffffff:08x}',
                        'stdout':probe.stdout, 'stderr':probe.stderr}
        probe_record['loaded_runtime_modules'] = [
            {'path':path, 'sha256':sha256(path) if Path(path).is_file() else None}
            for path in re.findall(r'^loaded_runtime_module=(.+)$', probe.stderr, re.M)]
        if probe.returncode:
            if label == 'normal':
                raise RuntimeError(f'Normal OpenMP runtime probe failed: {probe_record}')
            print(f'{label}: SKIPPED_RUNTIME_FAILURE ({probe_record["returncode_hex"]})', flush=True)
            validations.append({'mode':label, 'environment':overrides,
                                'status':'SKIPPED_RUNTIME_FAILURE', 'probe':probe_record,
                                'reason':'Independent omp-only probe failed before solver execution; root cause (compiler/runtime/DLL configuration) not established.'})
            continue
        output = run([binaries['test']], env=case_env)
        print(f'{label}: {output}', flush=True)
        validations.append({'mode':label, 'environment':overrides, 'status':'PASS',
                            'probe':probe_record, 'output':output})
    print('Benchmarking sequentially (2 warmups + 7 samples, API and prepared scopes)...', flush=True)
    benchmark = json.loads(run([binaries['bench']], env=env, timeout=240))
    hash_paths = set(sources + ['tests/test_dijkstra_openmp.cpp', 'tests/openmp_runtime_probe.cpp',
                               'benchmarks/bench_dijkstra_openmp.cpp', 'run_dijkstra_openmp.py'])
    hash_paths.update(str(p.relative_to(ROOT)).replace('\\', '/') for p in (ROOT/'include').rglob('*')
                      if p.suffix in {'.hpp', '.h'})
    fingerprints = {p:sha256(ROOT/p) for p in sorted(hash_paths)}
    repo = ROOT.parent.as_posix()
    git = ['git', '-c', f'safe.directory={repo}', '-C', repo]
    try:
        commit = run([*git, 'rev-parse', 'HEAD'], env=env)
        status = run([*git, 'status', '--short'], env=env)
    except (RuntimeError, OSError):
        commit, status = None, 'unavailable'
    loaded = []
    for path in benchmark.get('runtime_modules', []):
        module = Path(path)
        loaded.append({'path':str(module), 'sha256':sha256(module) if module.is_file() else None})
    solver = (ROOT/'src/baseline/dijkstra_openmp.cpp').read_text(encoding='utf-8')
    result = {
        'schema_version':2, 'recorded_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'windows_error_dialogs':'Disabled for this runner and child processes only; crash exit codes remain failures.',
        'platform':platform.platform(), 'machine':platform.machine(),
        'processor':platform.processor(), 'logical_cpus':os.cpu_count(),
        'compiler':compiler_version, 'compiler_path':str(compiler), 'compiler_sha256':compiler_digest,
        'build_directory':str(build), 'flags':flags, 'compile_logs':compile_logs,
        'git_commit':commit, 'git_status':status,
        'environment':{k:v for k,v in env.items() if k.startswith(RUNTIME_PREFIXES)},
        'removed_inherited_runtime_environment':removed,
        'loaded_runtime_modules':loaded,
        'runtime_import_inspection':imported_libraries(binaries['bench'], compiler.parent, env),
        'runtime_note':'Loaded module paths observed inside benchmark process. Empty module list may mean static runtime linkage; inspect PE imports. Probe failure does not identify the root cause.',
        'timer':'std::chrono::steady_clock; start/stop on the main caller thread; wall elapsed time, not clock()/CPU time.',
        'timing_scope':{
            'api':'Entire solver call on original CSR: result/workspace allocation and initialization, validation/normalization for serial scan and OpenMP, OpenMP team entry/exit, and cleanup of internal workspace/normalized temporary. Existing heap does not validate CSR.',
            'sssp_prepared':'Solve using the same already-validated/normalized CSR snapshot. Includes result/workspace allocation and initialization, workspace cleanup and OpenMP team entry/exit. Excludes validation/normalization. Serial scan contains no OpenMP construct.',
            'preparation':'PreparedDijkstraGraph construction only: validation, row sorting, duplicate-edge coalescing, owning CSR allocation; excludes destruction.',
            'excluded':'Input generation, file IO, result verification and result destruction. API/prepared speedups use serial scan of the SAME scope; preparation median is not subtracted from API median.'},
        'structural_review':{
            'parallel_directives_in_solver':len(re.findall(r'^\s*#pragma omp parallel\b',solver,re.M)),
            'critical_directives_in_solver':len(re.findall(r'^\s*#pragma omp critical\b',solver,re.M)),
            'organization':'One persistent parallel region encloses all vertex-selection iterations. Per-thread minima merged by single; no critical per candidate. Selection, single publication, and relaxation retain phase barriers.',
            'granularity':'Graph-level mean/max outgoing degree and per-probe-team vertices/relax-edges recorded; this is a workload-size diagnostic, not a measured barrier-cost attribution.'},
        'source_sha256':fingerprints, 'validation':validations, 'benchmark':benchmark}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
    print(f'Saved {args.output}')
    for case in benchmark['cases']:
        print(f"{case['family']} n={case['n']} m={case['m']} preparation={case['preparation']['median_ms']:.4f} ms")
        for mode in case['modes']:
            print(f"  {mode['scope']} {mode['mode']}: {mode['median_ms']:.4f} ms; speedup_vs_scan={mode['speedup_vs_scan']:.3f}x")


if __name__ == '__main__':
    main()
