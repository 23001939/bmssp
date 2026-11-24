import gzip
import os
import shutil
import time
import urllib.request

OUT_DIR = "bmssp_parallel/data/snap"
os.makedirs(OUT_DIR, exist_ok=True)

DATASETS = [
    ("wiki-topcats", "https://snap.stanford.edu/data/wiki-topcats.txt.gz", "wiki-topcats.txt"),
    ("soc-Pokec", "https://snap.stanford.edu/data/soc-pokec-relationships.txt.gz", "soc-Pokec.txt"),
]

for name, url, txt_name in DATASETS:
    txt_path = os.path.join(OUT_DIR, txt_name)
    gz_path = txt_path + ".gz"
    if os.path.exists(txt_path) and os.path.getsize(txt_path) > 100_000_000:
        print(f"[SKIP] {name} already extracted: {txt_path} ({os.path.getsize(txt_path)/1e6:.1f} MB)", flush=True)
        continue

    t0 = time.perf_counter()
    print(f"[*] Downloading {name} from {url} ...", flush=True)
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=120) as resp, open(gz_path, "wb") as out_gz:
        shutil.copyfileobj(resp, out_gz, length=4 * 1024 * 1024)
    t_dl = time.perf_counter() - t0
    print(f"    Downloaded {os.path.getsize(gz_path)/1e6:.2f} MB in {t_dl:.1f}s. Extracting...", flush=True)

    t1 = time.perf_counter()
    with gzip.open(gz_path, "rb") as gz_in, open(txt_path, "wb") as txt_out:
        shutil.copyfileobj(gz_in, txt_out, length=8 * 1024 * 1024)
    os.remove(gz_path)
    print(f"[OK] {name} -> {txt_path} ({os.path.getsize(txt_path)/1e6:.2f} MB, extract {time.perf_counter()-t1:.1f}s)", flush=True)
