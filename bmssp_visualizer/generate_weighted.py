"""Reproducible sparse weighted grid, same family as weighted_100."""
import argparse
import json
import math
from pathlib import Path
import random
from data import normalize_graph, dijkstra

ROOT=Path(__file__).resolve().parent


def generate(n,seed=20261114):
    if not 10<=n<=50000:
        raise ValueError('n must be between 10 and 50000')
    cols=math.ceil(math.sqrt(n))
    rows=math.ceil(n/cols)
    rng=random.Random(seed)
    pairs=set()
    edges=[]
    def add(u,v):
        if (u,v) not in pairs:
            pairs.add((u,v))
            edges.append([u,v,rng.randint(1,20)])
    for u in range(n):
        if u%cols<cols-1 and u+1<n:
            add(u,u+1)
        if u+cols<n:
            add(u,u+cols)
    reverse=[(v,u) for u,v in sorted(pairs)]
    rng.shuffle(reverse)
    for u,v in reverse[:int(.3*n)]:
        add(u,v)
    target_edges=round(2.4*n)
    diagonals=[(u,u+cols+1) for u in range(n)
               if u%cols<cols-1 and u+cols+1<n]
    rng.shuffle(diagonals)
    for u,v in diagonals:
        if len(edges)>=target_edges:
            break
        add(u,v)
    positions=[{'x':60+(u%cols)*882/max(1,cols-1),
                'y':50+(u//cols)*585/max(1,rows-1)} for u in range(n)]
    return normalize_graph({'id':f'weighted_{n}','name':f'Đồ thị chung · {n} đỉnh có trọng số',
                            'n':n,'source':0,'target':n-1,'edges':edges,'positions':positions,
                            'metadata':{'seed':seed,'kind':'weighted_grid_local',
                                        'columns':cols,'rows':rows,
                                        'description':'Sparse directed grid with reverse edges and diagonal shortcuts, weights 1..20.'}})


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--n',type=int,required=True)
    parser.add_argument('--seed',type=int,default=20261114)
    args=parser.parse_args()
    graph=generate(args.n,args.seed)
    directory=ROOT/'data'
    directory.mkdir(exist_ok=True)
    (directory/f'weighted_{args.n}.json').write_text(json.dumps(graph,ensure_ascii=False,indent=2),encoding='utf-8')
    (directory/f'weighted_{args.n}.txt').write_text(f"{graph['n']} {len(graph['edges'])}\n"+
        ''.join(f"{e['u']} {e['v']} {e['w']:.17g}\n" for e in graph['edges']),encoding='utf-8')
    dist,_,_=dijkstra(graph,0)
    assert all(d is not None for d in dist)
    print(json.dumps({'n':args.n,'m':len(graph['edges']),'target':args.n-1,
                      'distance':dist[-1],'seed':args.seed,'all_reachable':True}))


if __name__=='__main__':main()
