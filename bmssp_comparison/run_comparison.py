"""Deterministic common-input benchmark. Run sequentially to avoid CPU contention."""
from __future__ import annotations
import json
import math
from pathlib import Path
import platform
import random
import statistics
import subprocess
import sys
from datetime import datetime, timezone
from bench_python import oracle

ROOT = Path(__file__).resolve().parent
DATA = ROOT/'data'
DATA.mkdir(exist_ok=True)

def git_commit(repo):
    gitdir=repo/'.git'
    head=(gitdir/'HEAD').read_text().strip()
    if not head.startswith('ref: '):
        return head
    ref=head[5:]
    loose=gitdir/ref
    if loose.exists():
        return loose.read_text().strip()
    for line in (gitdir/'packed-refs').read_text().splitlines():
        if line.endswith(' '+ref):
            return line.split()[0]
    raise RuntimeError(f'Cannot resolve commit for {repo}')

def make_case(kind,n):
    rng=random.Random(20261113+n)
    edges=[]
    if kind=='counterexample':
        edges=[(0,1,.9),(0,2,.1),(2,1,.1),(1,3,.1),(0,3,2.0)]
        edges.extend((i,i+1,1.0) for i in range(3,n-2))
    else:
        edges=[(i,i+1,float(rng.randrange(1,11))) for i in range(n-2)]
        # Three random permutations keep BOTH degrees bounded by a constant.
        for _ in range(3):
            permutation=list(range(n-1)); rng.shuffle(permutation)
            for u,v in enumerate(permutation):
                w=(rng.randrange(0,5) if kind=='zero_ties' else
                   rng.randrange(1,101) if kind=='integer' else rng.uniform(.01,10.0))
                edges.append((u,v,float(w)))
    # Far sink makes source-to-target variants process all original reachable vertices.
    distances=oracle(n,edges)()
    far=max(d for d in distances if math.isfinite(d))+10.0
    edges.append((0,n-1,far))
    return {'n':n,'edges':edges,'seed':20261113+n,'kind':kind}

def call(args,timeout=45):
    try:
        p=subprocess.run(args,cwd=ROOT,capture_output=True,text=True,timeout=timeout)
        if p.returncode:
            return {'status':'ERROR','returncode':p.returncode,'stderr':p.stderr[-1000:]}
        return json.loads(p.stdout.strip().splitlines()[-1])
    except subprocess.TimeoutExpired:
        return {'status':'TIMEOUT','timeout_seconds':timeout}
    except Exception as exc:
        return {'status':'ERROR','error':str(exc)}

def main():
    specs=[('counterexample',1500),('real',2000),('real',10000),('integer',10000),('zero_ties',10000),('real',50000)]
    if '--smoke' in sys.argv:
        specs=specs[:2]
    results={'created_utc':datetime.now(timezone.utc).isoformat(),
             'platform':platform.platform(),'python':sys.version,'cases':[], 'commits':{}}
    for repo in (ROOT/'repos').iterdir():
        results['commits'][repo.name]=git_commit(repo)
    for kind,n in specs:
        data=make_case(kind,n)
        key=f'{kind}_{n}'
        jp=DATA/f'{key}.json'; tp=DATA/f'{key}.txt'
        jp.write_text(json.dumps(data),encoding='utf-8')
        with tp.open('w',encoding='utf-8') as f:
            f.write(f"{n} {len(data['edges'])}\n")
            for u,v,w in data['edges']: f.write(f'{u} {v} {w:.17g}\n')
        expected=oracle(n,data['edges'])()
        case={'name':key,'n':n,'m':len(data['edges']),'algorithms':{}}
        print(f'CASE {key} n={n} m={len(data["edges"])}',flush=True)
        for mode in ['dijkstra','wc_bounded','expected_bounded','wc','expected']:
            out=DATA/f'{key}_{mode}_dist.txt'
            r=call([str(ROOT/'bench_cpp.exe'),mode,str(tp),str(out)])
            if 'samples_ms' in r:
                got=[float(s) for s in out.read_text().splitlines()]
                bad=[i for i,(a,b) in enumerate(zip(got,expected)) if not math.isclose(a,b,rel_tol=1e-9,abs_tol=1e-9)]
                r['status']='PASS' if len(got)==n and not bad else 'WRONG'
                r['wrong_vertices']=len(bad)
                r['median_ms']=statistics.median(r['samples_ms'])
                r['median_kernel_ms']=statistics.median(r['kernel_ms'])
            case['algorithms']['cpp_'+mode]=r
            print(' cpp_'+mode,json.dumps(r),flush=True)
        modes=['python_dijkstra','nobelk','bzantium_v1','bzantium_v2','logtwothirds_reference']
        for mode in modes:
            # Pure-Python instrumented oracle is not the Rust implementation.
            if mode=='logtwothirds_reference' and n>10000:
                r={'status':'SKIP','reason':'Pure Python reference only; Rust toolchain unavailable; limit reference workload.'}
            else:
                r=call([sys.executable,str(ROOT/'bench_python.py'),mode,str(jp)])
            case['algorithms'][mode]=r
            print(' '+mode,json.dumps(r),flush=True)
        results['cases'].append(case)
        (ROOT/('smoke_results.json' if '--smoke' in sys.argv else 'results.json')).write_text(json.dumps(results,indent=2),encoding='utf-8')

if __name__=='__main__': main()
