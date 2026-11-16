"""
GĐ2 + GĐ3: Cross-check C++ vs Rust distances + benchmark toàn diện.
Chạy: python -X utf8 bmssp_comparison/cross_check_and_bench.py
"""
from __future__ import annotations
import json, math, os, random, statistics, subprocess, sys, time, heapq
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DATA = ROOT / 'data'
DATA.mkdir(exist_ok=True)
BENCH_CPP = ROOT / 'bench_cpp.exe'
BENCH_RUST = ROOT / 'repos' / 'rust_python' / 'target' / 'release' / 'examples' / 'bench_rust.exe'

def _w(rng, wt):
    if wt == 'integer': return float(rng.randrange(1, 101))
    elif wt == 'zero_ties': return float(rng.randrange(0, 5))
    return rng.uniform(0.01, 10.0)

def gen_sparse(n, seed, wt='real'):
    rng = random.Random(seed); edges = []
    for i in range(n-1): edges.append((i, i+1, _w(rng, wt)))
    for _ in range(3):
        p = list(range(n-1)); rng.shuffle(p)
        for u, v in enumerate(p): edges.append((u, v, _w(rng, wt)))
    return edges

def gen_dense(n, seed, wt='real'):
    rng = random.Random(seed); edges = []
    for i in range(n-1): edges.append((i, i+1, _w(rng, wt)))
    for _ in range(9*n):
        u, v = rng.randrange(n), rng.randrange(n)
        if u != v: edges.append((u, v, _w(rng, wt)))
    return edges

def gen_grid(s, seed, wt='real'):
    rng = random.Random(seed); n = s*s; edges = []
    for r in range(s):
        for c in range(s):
            v = r*s+c
            if c+1<s: edges+=[(v,v+1,_w(rng,wt)),(v+1,v,_w(rng,wt))]
            if r+1<s: edges+=[(v,v+s,_w(rng,wt)),(v+s,v,_w(rng,wt))]
    return edges

def gen_star(n, seed, wt='real'):
    rng = random.Random(seed); edges = []
    for i in range(1,n): edges+=[(0,i,_w(rng,wt)),(i,0,_w(rng,wt))]
    for _ in range(n):
        u, v = rng.randrange(1,n), rng.randrange(1,n)
        if u!=v: edges.append((u,v,_w(rng,wt)))
    return edges

def gen_chain(n, seed, wt='real'):
    rng = random.Random(seed); edges = []
    for i in range(n-1): edges.append((i,i+1,_w(rng,wt)))
    for _ in range(n//10):
        u, v = rng.randrange(n), rng.randrange(n)
        if u!=v: edges.append((u,v,_w(rng,wt)))
    return edges

GENS = {'sparse': gen_sparse, 'dense': gen_dense, 'grid': gen_grid,
        'star': gen_star, 'chain': gen_chain}

def oracle(n, edges):
    adj = [[] for _ in range(n)]
    for u, v, w in edges: adj[u].append((v, w))
    d = [math.inf]*n; d[0]=0.0; q=[(0.0,0)]
    while q:
        du,u=heapq.heappop(q)
        if du!=d[u]: continue
        for v,w in adj[u]:
            nd=du+w
            if nd<d[v]: d[v]=nd; heapq.heappush(q,(nd,v))
    return d

def write_graph(path, n, edges):
    with open(path,'w') as f:
        f.write(f"{n} {len(edges)}\n")
        for u,v,w in edges: f.write(f"{u} {v} {w:.17g}\n")

def read_dist(path):
    with open(path) as f: return [float(l) for l in f if l.strip()]

def cmp_dist(d1, d2, tol=1e-9):
    if len(d1)!=len(d2): return list(range(max(len(d1),len(d2))))
    return [i for i,(a,b) in enumerate(zip(d1,d2))
            if not (math.isinf(a) and math.isinf(b))
            and not math.isclose(a,b,rel_tol=tol,abs_tol=tol)]

def run_bench(exe, mode, inp, out, timeout=120):
    try:
        p = subprocess.run([str(exe),mode,str(inp),str(out)],
            capture_output=True, text=True, timeout=timeout, cwd=str(ROOT))
        if p.returncode!=0:
            return {'status':'ERROR','rc':p.returncode,'err':p.stderr[-300:]}
        return {**json.loads(p.stdout.strip().splitlines()[-1]), 'status':'PASS'}
    except subprocess.TimeoutExpired:
        return {'status':'TIMEOUT'}
    except Exception as e:
        return {'status':'ERROR','error':str(e)}

def make_graph(gtype, param, wt, seed):
    if gtype=='grid':
        return param*param, GENS[gtype](param, seed, wt)
    return param, GENS[gtype](param, seed, wt)



def phase1_crosscheck():
    """Cross-check C++ vs Rust on 30 diverse graphs."""
    print("\n▶ PHASE 1: Cross-check C++ vs Rust")
    specs = [
        ('sparse',100,'real'),('sparse',500,'real'),('sparse',1000,'real'),
        ('sparse',2000,'real'),('sparse',3000,'real'),('sparse',5000,'real'),
        ('sparse',1000,'integer'),('sparse',1000,'zero_ties'),
        ('dense',100,'real'),('dense',500,'real'),('dense',1000,'real'),
        ('dense',2000,'real'),('grid',10,'real'),('grid',20,'real'),
        ('grid',32,'real'),('grid',50,'real'),('grid',71,'real'),
        ('star',200,'real'),('star',1000,'real'),('star',3000,'real'),
        ('chain',500,'real'),('chain',2000,'real'),('chain',5000,'real'),
        ('sparse',500,'integer'),('sparse',2000,'zero_ties'),
        ('dense',500,'integer'),('grid',20,'integer'),('star',500,'zero_ties'),
        ('sparse',4000,'real'),('dense',3000,'real'),
    ]
    results=[]; ok=True
    for i,(gt,p,wt) in enumerate(specs):
        seed=20261114+i*7; n,edges=make_graph(gt,p,wt,seed)
        label=f"{gt}_{p}_{wt}"; txt=DATA/f'xc_{label}.txt'
        write_graph(txt,n,edges); ref=oracle(n,edges)
        checks={}
        for name,exe,mode in [('cpp_wc',BENCH_CPP,'wc_bounded'),
                               ('rust_bmssp',BENCH_RUST,'bmssp'),
                               ('rust_dj',BENCH_RUST,'dijkstra')]:
            df=DATA/f'xc_{label}_{name}.dist'
            r=run_bench(exe,mode,txt,df)
            if r['status']!='PASS':
                checks[name]='ERROR'; ok=False; continue
            bad=cmp_dist(ref,read_dist(str(df)))
            checks[name]='OK' if not bad else f'WRONG({len(bad)})'
            if bad: ok=False
        # C++ vs Rust
        cf=DATA/f'xc_{label}_cpp_wc.dist'; rf=DATA/f'xc_{label}_rust_bmssp.dist'
        if checks.get('cpp_wc')=='OK' and checks.get('rust_bmssp')=='OK':
            bad=cmp_dist(read_dist(str(cf)),read_dist(str(rf)))
            checks['cpp_vs_rust']='OK' if not bad else f'MISMATCH({len(bad)})'
            if bad: ok=False
        sc=''.join('✓' if 'OK' in str(v) else '✗' for v in checks.values())
        results.append({'graph':label,'n':n,'m':len(edges),'checks':checks})
        print(f"  [{i+1:2d}/{len(specs)}] {label:30s} n={n:5d} m={len(edges):7d} {sc}")
    print(f"  → {'ALL OK ✓' if ok else 'FAILURES ✗'}")
    return results, ok

def phase2_fuzz():
    """Extended fuzz: 50 random graphs n=100..5000."""
    print("\n▶ PHASE 2: Extended fuzz (50 graphs)")
    rng=random.Random(20261114); results=[]; ok=True
    for i in range(50):
        n=rng.choice([100,200,500,1000,2000,3000,5000])
        wt=rng.choice(['real','integer','zero_ties'])
        gt=rng.choice(['sparse','dense','star','chain'])
        seed=20261114+1000+i*13
        nn,edges=make_graph(gt,n,wt,seed)
        label=f"fuzz{i}_{gt}_{n}_{wt}"; txt=DATA/f'{label}.txt'
        write_graph(txt,nn,edges); ref=oracle(nn,edges)
        df=DATA/f'{label}.dist'
        r=run_bench(BENCH_CPP,'wc_bounded',txt,df,timeout=180)
        st='ERROR'
        if r['status']=='PASS':
            bad=cmp_dist(ref,read_dist(str(df)))
            st='OK' if not bad else f'WRONG({len(bad)})'
        else: st=r['status']
        if st!='OK': ok=False
        results.append({'graph':label,'n':nn,'status':st})
        if (i+1)%10==0: print(f"  [{i+1}/50]")
    cnt=sum(1 for r in results if r['status']=='OK')
    print(f"  → Fuzz: {cnt}/50 OK {'✓' if ok else '✗'}")
    return results, ok



def phase3_bench():
    """Benchmark C++ and Rust across graph families and sizes."""
    print("\n▶ PHASE 3: Benchmark")
    specs = [
        ('sparse',1500,'real'),('sparse',5000,'real'),
        ('sparse',10000,'real'),('sparse',20000,'real'),
        ('sparse',50000,'real'),
        ('dense',1000,'real'),('dense',5000,'real'),
        ('grid',32,'real'),('grid',71,'real'),('grid',100,'real'),
        ('grid',224,'real'),
        ('star',1000,'real'),('star',5000,'real'),
        ('chain',5000,'real'),('chain',10000,'real'),
        ('chain',50000,'real'),
        ('sparse',10000,'integer'),('sparse',10000,'zero_ties'),
    ]
    MODES = [
        ('cpp_dj',  BENCH_CPP, 'dijkstra'),
        ('cpp_wc',  BENCH_CPP, 'wc_bounded'),
        ('cpp_exp', BENCH_CPP, 'expected_bounded'),
        ('rs_dj',   BENCH_RUST,'dijkstra'),
        ('rs_bm',   BENCH_RUST,'bmssp'),
        ('rs_fast', BENCH_RUST,'bmssp-fast'),
    ]
    results=[]
    for gi,(gt,p,wt) in enumerate(specs):
        seed=20261114+5000+gi*17; n,edges=make_graph(gt,p,wt,seed)
        label=f"{gt}_{p}_{wt}"; txt=DATA/f'b_{label}.txt'
        write_graph(txt,n,edges)
        entry={'graph':label,'n':n,'m':len(edges),'t':{}}
        for mn,exe,ma in MODES:
            to=300 if n>30000 and 'bm' in mn else 120
            df=DATA/f'b_{label}_{mn}.dist'
            r=run_bench(exe,ma,txt,df,timeout=to)
            if r['status']=='PASS':
                med=r.get('median_ms',statistics.median(r.get('samples_ms',[0])))
                entry['t'][mn]={'med':round(med,3),'s':r.get('samples_ms',[])}
            else:
                entry['t'][mn]={'status':r.get('status','ERR')}
        results.append(entry)
        parts=[]
        for mn,_,_ in MODES:
            v=entry['t'].get(mn,{})
            val = '—' if 'status' in v else f"{v['med']:.1f}"
            parts.append(f"{mn}={val}")
        print(f"  [{gi+1:2d}/{len(specs)}] {label:30s} n={n:6d}  {' | '.join(parts)}")
    return results

def main():
    print("="*70)
    print("BMSSP Cross-check & Benchmark Suite")
    print(f"Started: {datetime.now().isoformat()}")
    print("="*70)
    xc, xc_ok = phase1_crosscheck()
    fz, fz_ok = phase2_fuzz()
    bn = phase3_bench()
    out = {
        'created_utc': datetime.now(timezone.utc).isoformat(),
        'cross_check': xc, 'cross_check_ok': xc_ok,
        'fuzz': fz, 'fuzz_ok': fz_ok,
        'benchmark': bn,
    }
    op = ROOT/'full_results.json'
    op.write_text(json.dumps(out, indent=2, ensure_ascii=False), encoding='utf-8')
    print(f"\n✓ Saved → {op}")
    # Summary table
    MODES=[('cpp_dj','cpp_wc','cpp_exp','rs_dj','rs_bm','rs_fast')]
    hdr=f"{'Graph':30s}"
    for m in MODES[0]: hdr+=f" {m:>10s}"
    print("\n"+hdr+"\n"+"-"*len(hdr))
    for e in bn:
        row=f"{e['graph']:30s}"
        for m in MODES[0]:
            v=e['t'].get(m,{})
            row+=f" {'—':>10s}" if 'status' in v else f" {v['med']:10.3f}"
        print(row)
    print(f"\nFinished: {datetime.now().isoformat()}")

if __name__=='__main__':
    main()


