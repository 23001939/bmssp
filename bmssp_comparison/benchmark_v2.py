"""Versioned, bounded benchmark. python bmssp_comparison/benchmark_v2.py"""
import argparse, hashlib, json, math, platform, random, statistics, subprocess, time, sys
from pathlib import Path
from cross_check_and_bench import make_graph, oracle, write_graph

ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT.parent/'bmssp_visualizer'))
from data import normalize_graph, verify_result
EXE=ROOT/'benchmark_v2.exe'
MODES=['dijkstra','bmssp','bmssp_transform']

def invoke(mode,path,profile=False):
    p=subprocess.run([str(EXE),mode,str(path),'profile' if profile else 'timing'],capture_output=True,text=True,timeout=30)
    if p.returncode: raise RuntimeError(p.stderr[-1000:])
    return json.loads(p.stdout)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--smoke',action='store_true');a=ap.parse_args()
    out=ROOT/'results_v2'/time.strftime('%Y%m%d-%H%M%S');out.mkdir(parents=True)
    flags=['-std=c++20','-O2','-DNDEBUG']
    subprocess.run(['g++',*flags,str(ROOT/'benchmark_v2.cpp'),'-o',str(EXE)],check=True)
    seeds=[20261114] if a.smoke else [20261114,20261115,20261116]
    sizes=[100] if a.smoke else [100,1000]
    report={'schema':2,'scope':'setup=copy/prepare; solve=execute including result arrays; parsed_to_result=setup+solve; excludes IO/serialization/destruction/verification. All modes return distances+raw predecessor; path certification is outside timer.',
      'limits':{'timeout_seconds_per_process':30,'max_n':1000,'warmup':2,'samples':7},
      'compiler':subprocess.check_output(['g++','--version'],text=True).splitlines()[0],'flags':flags,'platform':platform.platform(),
      'sources':{str(p.relative_to(ROOT.parent)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'benchmark_v2.cpp',ROOT/'benchmark_v2.py',ROOT.parent/'bmssp_visualizer/data.py',ROOT.parent/'bmssp_visualizer/native/bmssp-traced.hpp']},'cases':[]}
    for family in ['chain','grid','sparse','star','dense']:
      for size in sizes:
       for seed in seeds:
        param=math.isqrt(size) if family=='grid' else size
        n,edges=make_graph(family,param,'integer',seed)
        if family=='dense':
            rng=random.Random(seed)
            edges=[(u,(u+j)%n,rng.randrange(1,101)) for u in range(n) for j in range(1,min(n,math.isqrt(n)+1))]
        ref=oracle(n,edges);graph=normalize_graph({'n':n,'edges':edges})
        path=out/f'{family}-{size}-{seed}.txt';write_graph(path,n,edges)
        case={'family':family,'seed':seed,'n':n,'m':len(edges),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'modes':{}}
        for mode in MODES:case['modes'][mode]={'status':'PASS','samples':[]}
        # Rotate order; every subprocess performs one run, correctness checked each time.
        for rep in range(9):
         order=MODES[rep%3:]+MODES[:rep%3]
         for mode in order:
          entry=case['modes'][mode]
          if entry['status']!='PASS':continue
          try:
           r=invoke(mode,path);verification=verify_result(graph,0,r,comparison_mode='exact');dist=r.pop('distances');r.pop('predecessors');r.pop('predecessor_edges',None);r.pop('original_to_prepared',None)
           if mode=='bmssp_transform' and max(r['max_in'],r['max_out'])>2:raise ValueError('transformed degree exceeds 2')
           if dist!=[None if math.isinf(x) else x for x in ref]:raise ValueError('exact integer distances mismatch')
           if not verification['ok']:raise ValueError('raw predecessor/path failed exact verification: '+str(verification))
           if rep>=2:entry['samples'].append(r)
          except Exception as e:entry.update(status='FAIL',error=str(e))
        for mode,entry in case['modes'].items():
         if entry['status']=='PASS':
          entry['summary']={key:{'median':statistics.median(x[key] for x in entry['samples']),'min':min(x[key] for x in entry['samples']),'max':max(x[key] for x in entry['samples'])} for key in ['setup_ms','solve_ms','parsed_to_result_ms']}
          try:
           profile=invoke(mode,path,True);profile.pop('distances');profile.pop('predecessors');entry['separate_profile']=profile
          except Exception as e:entry['profile_error']=str(e)
        report['cases'].append(case)
        (out/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        print(f'{family} n={n} seed={seed}: '+', '.join(f'{m}={v["status"]}' for m,v in case['modes'].items()),flush=True)
    print(out/'results.json')
    return int(any(v['status']!='PASS' for c in report['cases'] for v in c['modes'].values()))
if __name__=='__main__':raise SystemExit(main())
