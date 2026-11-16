import os
import sys
import urllib.request
import time

BASE_URL = "https://raw.githubusercontent.com/sengorajkumar/gpu_graph_algorithms/master/input"
DATA_DIR = r"C:\ttss\bmssp_comparison\data"
os.makedirs(DATA_DIR, exist_ok=True)

target_txt = os.path.join(DATA_DIR, "USA-road-d.FLA.txt")

def download_file(url, out_path):
    print(f"Downloading {url} -> {out_path}...", flush=True)
    t0 = time.time()
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    with urllib.request.urlopen(req) as resp, open(out_path, 'wb') as f:
        total = int(resp.headers.get('Content-Length', 0))
        downloaded = 0
        chunk_size = 1024 * 1024 # 1MB
        while True:
            chunk = resp.read(chunk_size)
            if not chunk:
                break
            f.write(chunk)
            downloaded += len(chunk)
            pct = (downloaded / total * 100) if total else 0
            if downloaded % (5 * 1024 * 1024) < chunk_size:
                print(f"  {downloaded / 1024 / 1024:.1f} MB / {total / 1024 / 1024:.1f} MB ({pct:.1f}%)", flush=True)
    print(f"Done in {time.time() - t0:.2f}s", flush=True)

from_file = os.path.join(DATA_DIR, "USA-road-d.FLA.FROM.csv")
to_file = os.path.join(DATA_DIR, "USA-road-d.FLA.TO.csv")
w_file = os.path.join(DATA_DIR, "USA-road-d.FLA.W.csv")

if not os.path.exists(from_file) or os.path.getsize(from_file) < 1000:
    download_file(f"{BASE_URL}/USA-road-d.FLA.gr_FROM.csv", from_file)
if not os.path.exists(to_file) or os.path.getsize(to_file) < 1000:
    download_file(f"{BASE_URL}/USA-road-d.FLA.gr_TO.csv", to_file)
if not os.path.exists(w_file) or os.path.getsize(w_file) < 1000:
    download_file(f"{BASE_URL}/USA-road-d.FLA.gr_W.csv", w_file)

print("All CSVs downloaded. Assembling USA-road-d.FLA.txt...", flush=True)
t0 = time.time()

# Read CSV lists
with open(from_file, 'r', encoding='ascii') as f:
    from_nodes = [int(x.strip()) for x in f.read().split(',') if x.strip()]

with open(to_file, 'r', encoding='ascii') as f:
    to_nodes = [int(x.strip()) for x in f.read().split(',') if x.strip()]

with open(w_file, 'r', encoding='ascii') as f:
    weights = [int(x.strip()) for x in f.read().split(',') if x.strip()]

m = len(from_nodes)
assert len(to_nodes) == m and len(weights) == m, f"Mismatch: from={len(from_nodes)}, to={len(to_nodes)}, w={len(weights)}"

max_node = max(max(from_nodes), max(to_nodes))
n = max_node # 1-indexed to 0-indexed (0 to n-1)
print(f"Loaded m={m} edges, max_node={max_node}. Writing {target_txt}...", flush=True)

with open(target_txt, 'w', encoding='utf-8') as f:
    f.write(f"{n} {m}\n")
    for u, v, w in zip(from_nodes, to_nodes, weights):
        f.write(f"{u - 1} {v - 1} {w}\n")

file_size_mb = os.path.getsize(target_txt) / (1024 * 1024)
print(f"Successfully generated {target_txt}: {file_size_mb:.2f} MB ({m} edges, {n} vertices) in {time.time() - t0:.2f}s", flush=True)
