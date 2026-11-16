"""Measure the actual HTTP system; defaults to weighted_100, accepts --input."""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import statistics
import time
import urllib.request

ROOT=Path(__file__).resolve().parent


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--repeat',type=int,default=21)
    parser.add_argument('--warmup',type=int,default=3)
    parser.add_argument('--port',type=int,default=8765)
    parser.add_argument('--input',type=Path,help='Graph JSON to upload and measure instead of weighted_100')
    parser.add_argument('--source',type=int,default=0)
    args=parser.parse_args()
    if args.repeat<1 or args.warmup<0:
        raise ValueError('Invalid repeat/warmup count')
    base=f'http://127.0.0.1:{args.port}'
    def request(path,payload=None):
        body=None if payload is None else json.dumps(payload).encode('utf-8')
        req=urllib.request.Request(base+path,data=body,headers={'Content-Type':'application/json'} if body else {})
        with urllib.request.urlopen(req,timeout=20) as response:
            return json.load(response)
    if args.input:
        supplied=json.loads(args.input.read_text(encoding='utf-8'))
        uploaded=request('/api/graphs',{'graph':supplied})
        graph=uploaded['graph']
        graph_id=uploaded['id']
        graph_label=args.input.stem
    else:
        graph=request('/api/graph?id=weighted_100')
        graph_id=graph_label='weighted_100'
    if not 0<=args.source<graph['n']:
        raise ValueError('Source outside graph')
    report={'created_utc':datetime.now(timezone.utc).isoformat(),'graph':graph_label,'graph_id':graph_id,
            'n':graph['n'],'m':len(graph['edges']),'source':args.source,'mode':'fast',
            'warmup_per_algorithm':args.warmup,'measured_per_algorithm':args.repeat,
            'method':'HTTP API sequential runs; alternate order. Native compute excludes file parsing/serialization, control waits and rendering. Native setup differs per implementation. BMSSP baseline is separate pristine upstream measurement.',
            'samples':{'dijkstra':[],'bmssp':[]},'verified':True}
    reference=None
    hashes=set()
    for round_index in range(args.warmup+args.repeat):
        order=('dijkstra','bmssp') if round_index%2==0 else ('bmssp','dijkstra')
        for algorithm in order:
            created=request('/api/runs',{'graph_id':graph_id,'source':args.source,
                                        'algorithm':algorithm,'mode':'fast','delay_ms':0})
            rid=created['run_id']
            deadline=time.monotonic()+30
            while True:
                run=request('/api/runs/'+rid)
                if run['status'] in ('completed','error','stopped'):
                    break
                if time.monotonic()>deadline:
                    raise TimeoutError(rid)
                time.sleep(.005)
            if run['status']!='completed' or not run.get('result',{}).get('verified'):
                raise RuntimeError(run)
            result=run['result']
            if reference is None:
                reference=result['distances']
            if result['distances']!=reference:
                raise AssertionError('Different distance arrays')
            hashes.add(result['graph_hash'])
            if round_index>=args.warmup:
                report['samples'][algorithm].append({'run_id':rid,
                    'native_compute_ms':result['native_compute_ms'],
                    'upstream_baseline_ms':result.get('baseline_ms'),
                    'wall_ms':result['wall_ms'],
                    'control_wait_ms':result['control_wait_ms'],
                    'verified':result['verified']})
    assert len(hashes)==1
    report['graph_hash']=next(iter(hashes))
    report['target']=graph['n']-1
    report['target_distance']=reference[-1]
    if graph['n']==100:
        report['target_99_distance']=reference[99]
    report['summary']={}
    for algorithm,samples in report['samples'].items():
        durations=[s['native_compute_ms'] for s in samples]
        row={'median_native_ms':statistics.median(durations),'min_native_ms':min(durations),
             'max_native_ms':max(durations),'median_wall_ms':statistics.median(s['wall_ms'] for s in samples)}
        baseline=[s['upstream_baseline_ms'] for s in samples if s['upstream_baseline_ms'] is not None]
        if baseline:
            row['median_upstream_baseline_ms']=statistics.median(baseline)
        report['summary'][algorithm]=row
    report['native_ratio_bmssp_to_dijkstra']=report['summary']['bmssp']['median_native_ms']/report['summary']['dijkstra']['median_native_ms']
    output=ROOT/'exports'/graph_label/'timing-measured.json'
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'summary':report['summary'],'ratio':report['native_ratio_bmssp_to_dijkstra'],
                      'runs':2*(args.repeat+args.warmup),'verified':True,'file':str(output)},ensure_ascii=False))


if __name__=='__main__':
    main()
