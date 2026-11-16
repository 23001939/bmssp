"""Build the isolated instrumented WC runner when sources have changed."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent
NATIVE = ROOT / "native"

def ensure_built() -> Path:
    exe = NATIVE / "runner.exe"
    sources = [NATIVE / "runner.cpp", NATIVE / "bmssp-traced.hpp", *NATIVE.glob("external/**/*.h"), *NATIVE.glob("external/*.h")]
    if not exe.exists() or any(p.stat().st_mtime_ns > exe.stat().st_mtime_ns for p in sources):
        subprocess.run(["g++", "-std=c++20", "-O2", "-DNDEBUG", str(NATIVE / "runner.cpp"), "-o", str(exe)], check=True)
    return exe

def ensure_baseline_built() -> Path:
    """Build the separate no-hook executable against the original checkout."""
    exe = NATIVE / "baseline.exe"
    upstream = ROOT.parent / "bmssp_comparison" / "repos" / "cpp" / "include"
    sources = [NATIVE / "baseline.cpp", upstream / "bmssp-worst-case.hpp", *upstream.glob("external/**/*.h"), *upstream.glob("external/*.h")]
    if not exe.exists() or any(p.stat().st_mtime_ns > exe.stat().st_mtime_ns for p in sources):
        subprocess.run(["g++", "-std=c++20", "-O2", "-DNDEBUG", str(NATIVE / "baseline.cpp"), "-o", str(exe)], check=True)
    return exe

def ensure_dijkstra_built() -> Path:
    """Build the binary-heap Dijkstra runner."""
    exe = NATIVE / "dijkstra.exe"
    source = NATIVE / "dijkstra.cpp"
    if not exe.exists() or source.stat().st_mtime_ns > exe.stat().st_mtime_ns:
        subprocess.run(["g++", "-std=c++20", "-O2", "-DNDEBUG", str(source), "-o", str(exe)], check=True)
    return exe

if __name__ == "__main__":
    print(ensure_built())
