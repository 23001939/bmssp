"""Launcher failure propagation and input reproducibility; no native benchmarks."""
from contextlib import redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import random
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


launcher = load('remediation_launcher', ROOT/'run_all.py')
fidelity = load('remediation_fidelity', ROOT/'bmssp_comparison/run_fidelity_checks.py')


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)

    def step(self, returncode=0):
        return subprocess.CompletedProcess(['child'], returncode, stdout='ok', stderr='bad' if returncode else '')

    def test_process_exit_nonzero_propagates(self):
        with patch.object(launcher.subprocess, 'run', return_value=self.step(7)):
            result = launcher.process_step('child', ['child'])
        self.assertEqual((result['status'], result['exit_code']), ('FAIL', 7))
        self.assertEqual(launcher.summarize([result], True), ('FAIL', 1))

    def test_missing_tool_is_failure(self):
        with patch.object(launcher.subprocess, 'run', side_effect=FileNotFoundError('missing')):
            result = launcher.process_step('missing', ['missing'])
        self.assertEqual(result['status'], 'FAIL')
        self.assertEqual(result['error_kind'], 'missing_tool_or_os_error')

    def test_timeout_is_failure(self):
        with patch.object(launcher.subprocess, 'run', side_effect=subprocess.TimeoutExpired(['child'], 1)):
            result = launcher.process_step('timeout', ['child'], timeout=1)
        self.assertEqual(result['status'], 'FAIL')
        self.assertEqual(result['error_kind'], 'timeout')

    def args(self, quick=True, limit=2):
        return launcher.build_parser().parse_args((['--quick'] if quick else [])+['--limit', str(limit), '--seed', '99'])

    def payload(self, limit=2, solvers=None):
        solvers = solvers or ['cpp']
        return {'schema': fidelity.SCHEMA, 'status': 'PASS', 'accuracy': True, 'exit_code': 0,
                'config': {'seed': 99, 'limit': limit, 'solvers': solvers}, 'completed_cases': limit,
                'cases': [{'results': {name: {'status': 'PASS', 'accuracy': True} for name in solvers}} for _ in range(limit)]}

    def run_fidelity_manifest(self, payload, code=0, raw=None):
        script = self.directory/'run_fidelity_checks.py'
        script.touch()
        output = self.directory/'fidelity.json'
        def fake_step(*args, **kwargs):
            output.write_text(raw if raw is not None else json.dumps(payload), encoding='utf-8')
            return {'name': 'empirical_distance_checks', 'required': True,
                    'status': 'PASS' if code == 0 else 'FAIL', 'exit_code': code}
        with patch.object(launcher, 'COMP_DIR', self.directory), patch.object(launcher, 'process_step', side_effect=fake_step) as child:
            result = launcher.fidelity_step(self.args(), output)
        return result, child

    def test_invalid_child_json_fails_even_exit_zero(self):
        result, _ = self.run_fidelity_manifest(None, raw='{bad json')
        self.assertEqual(result['status'], 'FAIL')

    def test_accuracy_false_fails_even_exit_zero(self):
        payload = self.payload()
        payload['cases'][0]['results']['cpp']['accuracy'] = False
        result, _ = self.run_fidelity_manifest(payload)
        self.assertEqual(result['status'], 'FAIL')

    def test_zero_exit_cannot_hide_skipped_required_result(self):
        payload = self.payload()
        payload['cases'][0]['results']['cpp']['status'] = 'SKIPPED'
        result, _ = self.run_fidelity_manifest(payload)
        self.assertEqual(result['status'], 'FAIL')

    def test_child_nonzero_is_failure_despite_pass_manifest(self):
        result, _ = self.run_fidelity_manifest(self.payload(), code=8)
        self.assertEqual((result['status'], result['exit_code']), ('FAIL', 8))

    def test_wrong_config_or_missing_solver_is_failure(self):
        payload = self.payload()
        payload['config']['seed'] = 100
        self.assertEqual(self.run_fidelity_manifest(payload)[0]['status'], 'FAIL')
        payload = self.payload()
        payload['cases'][0]['results'] = {}
        self.assertEqual(self.run_fidelity_manifest(payload)[0]['status'], 'FAIL')

    def test_success_and_quick_actual_limit_seed_command(self):
        result, child = self.run_fidelity_manifest(self.payload())
        self.assertEqual(result['status'], 'PASS')
        command = child.call_args.args[1]
        self.assertEqual(command[command.index('--limit')+1], '2')
        self.assertEqual(command[command.index('--seed')+1], '99')
        self.assertEqual(command[command.index('--solvers')+1:], ['cpp'])

    def test_missing_script_fails_not_cached_pass(self):
        with patch.object(launcher, 'COMP_DIR', self.directory):
            result = launcher.fidelity_step(self.args(), self.directory/'out.json')
        self.assertEqual(result['status'], 'FAIL')

    def test_previous_manifest_cannot_satisfy_missing_new_output(self):
        (self.directory/'run_fidelity_checks.py').touch()
        output = self.directory/'output.json'
        output.write_text(json.dumps(self.payload()), encoding='utf-8')
        with patch.object(launcher, 'COMP_DIR', self.directory), patch.object(launcher, 'process_step', return_value={'status': 'PASS'}):
            result = launcher.fidelity_step(self.args(), output)
        self.assertEqual(result['status'], 'FAIL')
        self.assertFalse(output.exists())

    def run_main(self, options, step_status='PASS', fidelity_status='PASS', make_script=True):
        visualizer = self.directory/'visualizer'
        (visualizer/'tests').mkdir(parents=True, exist_ok=True)
        (visualizer/'tests/test_placeholder.py').touch()
        (visualizer/'native').mkdir(exist_ok=True)
        (visualizer/'native/test_native.py').touch()
        if make_script:
            (visualizer/'start_demo.py').touch()
        output = self.directory/'manifest.json'
        def step(name, *args, **kwargs):
            return {'name': name, 'required': True, 'status': step_status, 'exit_code': 0 if step_status == 'PASS' else 7,
                    'stderr': 'Ran 2 tests in 0.001s\nOK'}
        with patch.object(launcher, 'VISUALIZER_DIR', visualizer), patch.object(launcher, 'process_step', side_effect=step) as calls, patch.object(launcher, 'fidelity_step', return_value={'name': 'fidelity', 'required': True, 'status': fidelity_status, 'exit_code': 0 if fidelity_status == 'PASS' else 1}) as fid, redirect_stdout(io.StringIO()) as stream:
            code = launcher.main(options+['--output', str(output)])
        return code, json.loads(output.read_text(encoding='utf-8')), calls, fid, stream.getvalue()

    def test_quick_scope_skips_rust_and_does_not_launch_by_default(self):
        code, result, calls, _, text = self.run_main(['--quick'])
        self.assertEqual((code, result['status'], result['config']['limit']), (0, 'PASS', 10))
        self.assertNotIn('launch_demo', [call.args[0] for call in calls.call_args_list])
        self.assertIn('rust_checks', [s['name'] for s in result['steps'] if s['status'] == 'SKIPPED'])
        self.assertNotIn('100%', text)

    def test_demo_only_never_claims_verification(self):
        code, result, calls, fid, _ = self.run_main(['--demo-only', '--no-browser'])
        self.assertEqual((code, result['status']), (0, 'SKIPPED'))
        self.assertEqual([call.args[0] for call in calls.call_args_list], ['launch_demo'])
        fid.assert_not_called()
        self.assertEqual(result['verification_scope'], 'none (demo-only)')
        self.assertTrue(all(s['status'] == 'SKIPPED' for s in result['steps'] if s['name'] != 'launch_demo'))

    def test_demo_failure_and_missing_script_fail_exit(self):
        code, result, *_ = self.run_main(['--demo-only'], step_status='FAIL')
        self.assertEqual((code, result['status']), (1, 'FAIL'))
        (self.directory/'visualizer/start_demo.py').unlink()
        code, result, *_ = self.run_main(['--demo-only'], make_script=False)
        self.assertEqual((code, result['status']), (1, 'FAIL'))

    def test_required_failure_blocks_launch(self):
        code, result, calls, _, _ = self.run_main(['--quick', '--launch-demo'], fidelity_status='FAIL')
        self.assertEqual((code, result['status']), (1, 'FAIL'))
        self.assertNotIn('launch_demo', [call.args[0] for call in calls.call_args_list])

    def test_toolchain_failure_skips_required_work_and_exits_nonzero(self):
        code, result, _, fid, _ = self.run_main(['--quick'], step_status='FAIL')
        self.assertEqual((code, result['status']), (1, 'FAIL'))
        fid.assert_not_called()
        self.assertTrue(any(s['required'] and s['status'] == 'SKIPPED' for s in result['steps']))

    def test_history_is_always_skipped_and_checks_content(self):
        history = self.directory/'history.json'
        history.write_text('[]', encoding='utf-8')
        invalid = launcher.read_history(history)
        self.assertEqual(invalid['status'], 'SKIPPED')
        self.assertIn('history_error', invalid)
        history.write_text(json.dumps({'schema': 'old', 'config': {'seed': 7}, 'provenance': {'commit': 'old'}, 'accuracy': False}), encoding='utf-8')
        result = launcher.read_history(history)
        self.assertEqual(result['status'], 'SKIPPED')
        self.assertIs(result['historical_evidence']['accuracy'], False)


class FidelityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)

    def test_all_choices_are_seeded_and_limit_is_real(self):
        random.seed(1)
        first = fidelity.make_cases(4, 876)
        random.seed(9999)
        second = fidelity.make_cases(4, 876)
        self.assertEqual(first, second)
        self.assertEqual(len(first), 4)
        self.assertNotEqual(first, fidelity.make_cases(4, 877))
        for case in first:
            self.assertEqual(len(case['edges']), case['m'])

    def test_parser_honors_limit_seed_and_rejects_zero(self):
        args = fidelity.build_parser().parse_args(['--limit', '3', '--seed', '44', '--solvers', 'cpp'])
        self.assertEqual((args.limit, args.seed, args.solvers), (3, 44, ['cpp']))
        with redirect_stdout(io.StringIO()), patch('sys.stderr', new_callable=io.StringIO), self.assertRaises(SystemExit):
            fidelity.build_parser().parse_args(['--limit', '0'])

    def test_exact_near_tie_rejected_but_tolerance_explicit(self):
        expected, actual = [0, 0, 0], [0, 4e-11, 0]
        self.assertFalse(fidelity.compare_distances(expected, actual, 'exact')['accuracy'])
        result = fidelity.compare_distances(expected, actual, 'tolerance')
        self.assertTrue(result['accuracy'])
        self.assertEqual(result['exact_mismatches'], 1)

    def test_invalid_count_nan_and_unreachable_fail(self):
        for actual in ([0], [0, float('nan')], [0, float('inf')]):
            self.assertFalse(fidelity.compare_distances([0, 1], actual, 'exact')['accuracy'])

    def test_solver_nonzero_timeout_and_missing_output(self):
        for result in [{'status': 'FAIL', 'exit_code': 7}, {'status': 'FAIL', 'error_kind': 'timeout'}, {'status': 'PASS', 'exit_code': 0, 'stdout': '{"samples_ms":[1]}'}]:
            with patch.object(fidelity, 'command_info', return_value=result.copy()):
                info, values = fidelity.run_solver('exe', 'wc', self.directory/'in', self.directory/'out', 2, 1)
            self.assertEqual(info['status'], 'FAIL')
            self.assertIsNone(values)

    def test_solver_invalid_json_and_child_accuracy_false(self):
        for raw in ('bad', '{"accuracy":false,"samples_ms":[1]}', '{"samples_ms":[NaN]}'):
            with patch.object(fidelity, 'command_info', return_value={'status': 'PASS', 'stdout': raw}):
                info, _ = fidelity.run_solver('exe', 'wc', self.directory/'in', self.directory/'out', 2, 1)
            self.assertEqual(info['status'], 'FAIL')

    def test_stale_distance_file_is_removed(self):
        output = self.directory/'old.dist'
        output.write_text('0\n1\n')
        with patch.object(fidelity, 'command_info', return_value={'status': 'PASS', 'stdout': '{"samples_ms":[1]}'}):
            result, _ = fidelity.run_solver('exe', 'wc', self.directory/'in', output, 2, 1)
        self.assertEqual(result['status'], 'FAIL')
        self.assertFalse(output.exists())

    def test_missing_executable_is_fail_and_limit_preserved(self):
        args = fidelity.build_parser().parse_args(['--limit', '2', '--solvers', 'cpp', '--cpp-bin', str(self.directory/'missing'), '--output', str(self.directory/'result.json')])
        with patch.object(fidelity, 'provenance', return_value={}), redirect_stdout(io.StringIO()):
            result = fidelity.execute(args)
        self.assertEqual((result['status'], result['exit_code'], result['completed_cases']), ('FAIL', 1, 2))
        self.assertEqual(result['solvers']['cpp']['status'], 'FAIL')

    def test_nonrequired_known_failure_is_reported_separately(self):
        binary = self.directory/'fake.exe'
        binary.touch()
        args = fidelity.build_parser().parse_args(['--limit', '2', '--seed', '99', '--solvers', 'cpp', '--cpp-bin', str(binary), '--output', str(self.directory/'result.json')])
        def fake_solver(exe, mode, inp, out, n, timeout):
            lines = inp.read_text().splitlines()
            edges = [(int(u), int(v), float(w)) for u, v, w in (line.split() for line in lines[1:])]
            values = fidelity.bellman_ford({'n': n, 'source': 0, 'edges': edges})
            if n == 3:
                values[1] = 4e-11
            return {'status': 'PASS', 'exit_code': 0}, values
        with patch.object(fidelity, 'provenance', return_value={}), patch.object(fidelity, 'run_solver', side_effect=fake_solver), redirect_stdout(io.StringIO()):
            result = fidelity.execute(args)
        self.assertEqual((result['status'], result['exit_code'], len(result['cases'])), ('PASS', 0, 2))
        self.assertEqual(result['diagnostics'][0]['results']['cpp']['status'], 'FAIL')
        self.assertFalse(result['diagnostics'][0]['required'])
        self.assertEqual(result['diagnostic_failures'], 1)


if __name__ == '__main__':
    unittest.main()
