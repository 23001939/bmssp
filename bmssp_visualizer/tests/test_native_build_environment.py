"""Regression checks for compiler provenance, caching and native DLL lookup.

Use fake compiler files and subprocess calls: these tests neither load native
executables nor require a compiler installation or a listening HTTP server.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import build_native  # noqa: E402


class NativeBuildEnvironmentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.native = self.root / 'native'
        self.native.mkdir()
        (self.native / 'runner.cpp').write_text('int main() { return 0; }\n')
        (self.native / 'bmssp-traced.hpp').write_text('// initial header\n')
        self.compiler_a = self.make_compiler('compiler A')
        self.compiler_b = self.make_compiler('compiler B')
        self.compilations = []
        self.patches = [
            patch.object(build_native, 'NATIVE', self.native),
            patch.dict(os.environ, {'CXX': str(self.compiler_a), 'PATH': 'original-runtime-path'}),
            patch.object(build_native.subprocess, 'run', side_effect=self.fake_run),
            patch.object(build_native.subprocess, 'check_output', return_value='fake GNU C++ 1.0\n'),
            patch.object(build_native.shutil, 'which', side_effect=self.which),
        ]
        for mock_patch in self.patches:
            mock_patch.start()
            self.addCleanup(mock_patch.stop)

    def make_compiler(self, name):
        compiler = self.root / name / 'bin' / 'g++.exe'
        compiler.parent.mkdir(parents=True)
        compiler.write_bytes(b'fake compiler binary')
        return compiler

    def which(self, requested):
        candidate = Path(requested)
        return str(candidate) if candidate.exists() else None

    def fake_run(self, command, **kwargs):
        command = [str(argument) for argument in command]
        if '--version' in command:
            return subprocess.CompletedProcess(command, 0, stdout='fake GNU C++ 1.0\n', stderr='')
        self.assertIn('-o', command, 'compiler command must name its output')
        self.assertEqual(Path(kwargs['env']['PATH'].split(os.pathsep)[0]), Path(command[0]).parent)
        output = Path(command[command.index('-o') + 1])
        output.write_bytes(b'fake executable')
        self.compilations.append(command)
        return subprocess.CompletedProcess(command, 0, stdout='', stderr='')

    def test_runtime_path_comes_from_recorded_compiler_without_global_mutation(self):
        exe = build_native.ensure_built()
        # Simulate a different launching shell: provenance must win over CXX/PATH.
        os.environ['CXX'] = str(self.compiler_b)
        before = dict(os.environ)
        child = build_native.runtime_environment(exe)
        self.assertEqual(Path(child['PATH'].split(os.pathsep)[0]), self.compiler_a.parent)
        self.assertIn('original-runtime-path', child['PATH'].split(os.pathsep))
        self.assertEqual(dict(os.environ), before)
        child['PATH'] = 'modified child environment'
        self.assertEqual(dict(os.environ), before)
        self.assertEqual(len(self.compilations), 1)

    def test_unchanged_source_and_compiler_reuse_binary(self):
        first = build_native.ensure_built()
        second = build_native.ensure_built()
        self.assertEqual(first, second)
        self.assertEqual(len(self.compilations), 1)

    def test_changed_source_invalidates_cache_even_if_timestamp_is_preserved(self):
        build_native.ensure_built()
        source = self.native / 'runner.cpp'
        previous = source.stat()
        source.write_text('int main() { return 1; }\n')
        os.utime(source, ns=(previous.st_atime_ns, previous.st_mtime_ns))
        build_native.ensure_built()
        self.assertEqual(len(self.compilations), 2)

    def test_changed_header_invalidates_cache(self):
        build_native.ensure_built()
        header = self.native / 'bmssp-traced.hpp'
        previous = header.stat()
        header.write_text('// changed header\n')
        os.utime(header, ns=(previous.st_atime_ns, previous.st_mtime_ns))
        build_native.ensure_built()
        self.assertEqual(len(self.compilations), 2)

    def test_selected_compiler_change_rebuilds_and_updates_runtime_path(self):
        build_native.ensure_built()
        os.environ['CXX'] = str(self.compiler_b)
        exe = build_native.ensure_built()
        self.assertEqual(len(self.compilations), 2)
        self.assertEqual(Path(self.compilations[-1][0]), self.compiler_b)
        child = build_native.runtime_environment(exe)
        self.assertEqual(Path(child['PATH'].split(os.pathsep)[0]), self.compiler_b.parent)

    def test_compiler_replaced_at_same_path_invalidates_cache(self):
        build_native.ensure_built()
        previous = self.compiler_a.stat()
        self.compiler_a.write_bytes(b'new compiler binary')
        os.utime(self.compiler_a,
                 ns=(previous.st_atime_ns, previous.st_mtime_ns + 1_000_000_000))
        build_native.ensure_built()
        self.assertEqual(len(self.compilations), 2)

    def test_replaced_binary_does_not_reuse_stale_provenance(self):
        exe = build_native.ensure_built()
        exe.write_bytes(b'executable built by another compiler')
        build_native.ensure_built()
        self.assertEqual(len(self.compilations), 2)


if __name__ == '__main__':
    unittest.main()
