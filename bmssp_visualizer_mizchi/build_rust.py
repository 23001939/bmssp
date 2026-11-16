"""Build the local, pinned mizchi runners and return their executable paths."""
from __future__ import annotations

from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent
NATIVE = ROOT / 'native_rust'
BATCH_EXE = NATIVE / 'target' / 'release' / 'batch_runner.exe'
TRACED_EXE = NATIVE / 'target' / 'release' / 'traced_runner.exe'


def _sources() -> list[Path]:
    return [NATIVE / 'Cargo.toml', NATIVE / 'Cargo.lock', *NATIVE.glob('src/**/*.rs')]


def ensure_built() -> Path:
    """Build only when the pinned fork source is newer than batch_runner.exe."""
    if not NATIVE.is_dir():
        raise RuntimeError(f'Không tìm thấy Rust fork: {NATIVE}')
    if not BATCH_EXE.exists() or any(path.stat().st_mtime_ns > BATCH_EXE.stat().st_mtime_ns for path in _sources()):
        subprocess.run(
            ['cargo', 'build', '--release', '--bin', 'batch_runner'],
            cwd=NATIVE,
            check=True,
        )
    if not BATCH_EXE.is_file():
        raise RuntimeError(f'Cargo không tạo binary mong đợi: {BATCH_EXE}')
    return BATCH_EXE


def ensure_traced_built() -> Path:
    """Build only when the pinned fork source is newer than traced_runner.exe."""
    if not NATIVE.is_dir():
        raise RuntimeError(f'Không tìm thấy Rust fork: {NATIVE}')
    if not TRACED_EXE.exists() or any(path.stat().st_mtime_ns > TRACED_EXE.stat().st_mtime_ns for path in _sources()):
        subprocess.run(
            ['cargo', 'build', '--release', '--bin', 'traced_runner'],
            cwd=NATIVE,
            check=True,
        )
    if not TRACED_EXE.is_file():
        raise RuntimeError(f'Cargo không tạo binary mong đợi: {TRACED_EXE}')
    return TRACED_EXE


if __name__ == '__main__':
    print('Batch exe:', ensure_built())
    print('Traced exe:', ensure_traced_built())
