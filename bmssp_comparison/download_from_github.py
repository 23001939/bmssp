import os
import urllib.request
import time

datasets = [
    {
        "name": "BAY",
        "url": "https://raw.githubusercontent.com/lbwang95/QHL4CSP/main/data/BAY/USA-road-t.BAY.gr",
        "gr_path": "bmssp_comparison/data/USA-road-t.BAY.gr",
        "txt_path": "bmssp_comparison/data/USA-road-t.BAY.txt"
    },
    {
        "name": "COL",
        "url": "https://raw.githubusercontent.com/lbwang95/QHL4CSP/main/data/COL/USA-road-t.COL.gr",
        "gr_path": "bmssp_comparison/data/USA-road-t.COL.gr",
        "txt_path": "bmssp_comparison/data/USA-road-t.COL.txt"
    }
]

def download_and_convert(ds):
    name = ds["name"]
    url = ds["url"]
    gr_path = ds["gr_path"]
    txt_path = ds["txt_path"]
    
    # 1. Download
    if not os.path.exists(gr_path):
        print(f"[{name}] Downloading {url} -> {gr_path}...")
        t0 = time.time()
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=120) as resp, open(gr_path, "wb") as fout:
            chunk_size = 1024 * 1024
            while True:
                chunk = resp.read(chunk_size)
                if not chunk:
                    break
                fout.write(chunk)
        print(f"[{name}] Download complete ({os.path.getsize(gr_path)} bytes) in {time.time()-t0:.2f}s")
    else:
        print(f"[{name}] {gr_path} already exists ({os.path.getsize(gr_path)} bytes)")
        
    # 2. Convert to TXT (0-indexed: u-1, v-1, w)
    if not os.path.exists(txt_path):
        print(f"[{name}] Converting DIMACS to TXT -> {txt_path}...")
        t0 = time.time()
        n, m = 0, 0
        edges = []
        with open(gr_path, "r", encoding="utf-8", errors="ignore") as fin:
            for line in fin:
                if line.startswith("p sp "):
                    parts = line.strip().split()
                    n, m = int(parts[2]), int(parts[3])
                elif line.startswith("a "):
                    parts = line.strip().split()
                    u = int(parts[1]) - 1
                    v = int(parts[2]) - 1
                    w = parts[3]
                    edges.append((u, v, w))
        
        with open(txt_path, "w", encoding="utf-8") as fout:
            fout.write(f"{n} {len(edges)}\n")
            for u, v, w in edges:
                fout.write(f"{u} {v} {w}\n")
        print(f"[{name}] Finished TXT conversion: n={n}, m={len(edges)}, size={os.path.getsize(txt_path)} bytes in {time.time()-t0:.2f}s")
    else:
        print(f"[{name}] {txt_path} already exists ({os.path.getsize(txt_path)} bytes)")

for ds in datasets:
    download_and_convert(ds)
print("ALL DOWNLOADS & CONVERSIONS FINISHED SUCCESSFULLY!")
