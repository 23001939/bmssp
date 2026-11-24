#!/usr/bin/env python3
"""Download and extract the first 7 DIMACS-9 USA-road-t travel-time graphs (NY..CAL)."""
import gzip
import shutil
import sys
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

BASE_URLS = [
    "http://www.diag.uniroma1.it/~challenge9/data/USA-road-t",
    "https://www.diag.uniroma1.it/~challenge9/data/USA-road-t",
    "http://www.dis.uniroma1.it/~challenge9/data/USA-road-t",
]

NAMES = ["NY", "BAY", "COL", "FLA", "NW", "NE", "CAL"]


def download_and_extract(name: str, out_dir: Path) -> str:
    gr_path = out_dir / f"USA-road-t.{name}.gr"
    gz_path = out_dir / f"USA-road-t.{name}.gr.gz"
    if gr_path.exists() and gr_path.stat().st_size > 1_000_000:
        return f"[SKIP] {name}: already exists ({gr_path.stat().st_size / 1e6:.2f} MB)"

    last_err = None
    t0 = time.perf_counter()
    for base in BASE_URLS:
        url = f"{base}/USA-road-t.{name}.gr.gz"
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req, timeout=180) as resp, open(gz_path, "wb") as f:
                shutil.copyfileobj(resp, f)
            if gz_path.stat().st_size < 100_000:
                raise RuntimeError(f"Downloaded file too small ({gz_path.stat().st_size} B)")
            with gzip.open(gz_path, "rb") as fin, open(gr_path, "wb") as fout:
                shutil.copyfileobj(fin, fout)
            gz_mb = gz_path.stat().st_size / 1e6
            gr_mb = gr_path.stat().st_size / 1e6
            gz_path.unlink(missing_ok=True)
            dt = time.perf_counter() - t0
            return f"[OK] {name}: {gz_mb:.2f} MB .gz -> {gr_mb:.2f} MB .gr ({dt:.1f}s) from {base}"
        except Exception as exc:
            last_err = exc
            gz_path.unlink(missing_ok=True)
            gr_path.unlink(missing_ok=True)

    raise RuntimeError(f"Failed to download {name}: {last_err}")


def main():
    out_dir = Path("bmssp_parallel/data/dimacs_road")
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"[*] Downloading 7 DIMACS-9 road networks ({', '.join(NAMES)}) to {out_dir}...", flush=True)

    with ThreadPoolExecutor(max_workers=4) as ex:
        futs = {ex.submit(download_and_extract, nm, out_dir): nm for nm in NAMES}
        done = 0
        for fut in as_completed(futs):
            msg = fut.result()
            done += 1
            print(f"({done}/{len(NAMES)}) {msg}", flush=True)


if __name__ == "__main__":
    main()
