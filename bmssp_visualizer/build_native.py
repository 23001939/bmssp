"""Build native solvers with compiler provenance and runtime environments."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent
NATIVE = ROOT / "native"
FLAGS = ["-std=c++20", "-O2", "-DNDEBUG"]


def compiler_path() -> Path:
    requested = os.environ.get("CXX")
    found = shutil.which(requested or "g++")
    if found:
        return Path(found).resolve()
    if not requested:
        portable = ROOT.parents[1] / ".tools/w64devkit/bin/g++.exe"
        if portable.is_file():
            return portable.resolve()
    raise FileNotFoundError("C++20 compiler not found. Set CXX to the compiler executable or add g++ to PATH.")


def _compiler_environment(compiler: Path) -> dict:
    env = os.environ.copy()
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    return env


def runtime_environment(exe: Path) -> dict:
    """Use the compiler recorded for THIS binary, not server startup PATH."""
    record = json.loads(exe.with_suffix(".build.json").read_text(encoding="utf-8"))
    return _compiler_environment(Path(record["compiler_path"]))


def _digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _ensure(name: str, source: Path, sources: list[Path]) -> Path:
    exe = NATIVE / (name + ".exe")
    stamp = exe.with_suffix(".build.json")
    compiler = compiler_path()
    signature = {
        "compiler_path": str(compiler),
        "compiler_mtime_ns": compiler.stat().st_mtime_ns,
        "flags": FLAGS,
        "sources": {str(p.relative_to(NATIVE)): _digest(p) for p in sorted(set(sources))},
    }
    try:
        old = json.loads(stamp.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        old = {}
    if (not exe.exists() or old.get("signature") != signature or
            old.get("binary_sha256") != _digest(exe)):
        env = _compiler_environment(compiler)
        subprocess.run([str(compiler), *FLAGS, str(source), "-o", str(exe)], check=True, env=env)
        record = {
            "compiler_path": str(compiler), "signature": signature,
            "compiler_version": subprocess.check_output([str(compiler), "--version"], env=env, text=True),
            "binary_sha256": _digest(exe),
        }
        stamp.write_text(json.dumps(record, indent=2), encoding="utf-8")
    return exe


def ensure_built() -> Path:
    source = NATIVE / "runner.cpp"
    return _ensure("runner", source, [source, NATIVE / "bmssp-traced.hpp",
                                      *NATIVE.glob("external/**/*.h")])


def ensure_baseline_built() -> Path:
    source = NATIVE / "baseline.cpp"
    upstream = NATIVE / "upstream/include"
    return _ensure("baseline", source, [source, *upstream.rglob("*.hpp"), *upstream.rglob("*.h")])


def ensure_dijkstra_built() -> Path:
    source = NATIVE / "dijkstra.cpp"
    return _ensure("dijkstra", source, [source])


if __name__ == "__main__":
    print(ensure_built())
