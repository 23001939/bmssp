import zipfile
import urllib.request
import os
import io
import time

DATA_DIR = r"C:\ttss\bmssp_comparison\data"
out_txt = os.path.join(DATA_DIR, "USA-road-d.TX.txt")
zip_path = os.path.join(DATA_DIR, "TX.csv.zip")

url = "https://networks.skewed.de/net/us_roads/files/TX.csv.zip"

if not os.path.exists(zip_path) or os.path.getsize(zip_path) < 1000:
    print(f"Downloading {url} -> {zip_path}...", flush=True)
    t0 = time.time()
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    with urllib.request.urlopen(req) as resp, open(zip_path, 'wb') as f:
        total = int(resp.headers.get('Content-Length', 0))
        downloaded = 0
        chunk_size = 2 * 1024 * 1024
        while True:
            chunk = resp.read(chunk_size)
            if not chunk:
                break
            f.write(chunk)
            downloaded += len(chunk)
            pct = (downloaded / total * 100) if total else 0
            if downloaded % (10 * 1024 * 1024) < chunk_size:
                print(f"  {downloaded / 1024 / 1024:.1f} MB / {total / 1024 / 1024:.1f} MB ({pct:.1f}%)", flush=True)
    print(f"Downloaded in {time.time() - t0:.2f}s", flush=True)

print(f"Extracting and parsing {zip_path}...", flush=True)
t0 = time.time()
zf = zipfile.ZipFile(zip_path)

# First read nodes to find n
with zf.open("nodes.csv") as f:
    # count lines excluding header
    node_count = sum(1 for line in f if not line.startswith(b'#'))
print(f"Nodes found: {node_count}", flush=True)

# Read edges: source, target, travel_time, distance, road_category
# In DIMACS, distance is integer in tenths of meters or meters
# Let's read edges
edges = []
max_node = 0
with zf.open("edges.csv") as f:
    for line in f:
        if line.startswith(b'#'):
            continue
        parts = line.decode('ascii').strip().split(',')
        if len(parts) >= 4:
            u = int(parts[0])
            v = int(parts[1])
            # distance is float, e.g. 99.2597... Convert to integer (multiply by 10 or round)
            dist = max(1, int(round(float(parts[3]) * 10))) # tenths of meter, like DIMACS
            edges.append((u, v, dist))
            edges.append((v, u, dist)) # road segments are bidirectional
            if u > max_node: max_node = u
            if v > max_node: max_node = v

n = max(node_count, max_node + 1)
m = len(edges)
print(f"Total directed edges: {m}, vertices: {n}. Writing {out_txt}...", flush=True)

with open(out_txt, 'w', encoding='utf-8') as f:
    f.write(f"{n} {m}\n")
    for u, v, w in edges:
        f.write(f"{u} {v} {w}\n")

print(f"Saved {out_txt} ({os.path.getsize(out_txt) / 1024 / 1024:.2f} MB) in {time.time() - t0:.2f}s", flush=True)
