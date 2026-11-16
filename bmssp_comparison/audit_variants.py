"""Record actual base-case workload and a public-API correctness counterexample."""
import json
import math
from pathlib import Path
import sys
import types
from bench_python import ROOT, load_module

pkg=types.ModuleType('variant_src')
pkg.__path__=[str(ROOT/'repos/python_variants/src')]
sys.modules[pkg.__name__]=pkg
from variant_src.graph import Graph
from variant_src.bmssp_solver import BmsspSolver, BmsspSolverV2

edges=[(0,1,.9),(0,2,.1),(2,1,.1),(1,3,.1),(0,3,2.)]
g=Graph(1500)
for edge in edges: g.add_edge(*edge)
got=BmsspSolverV2(g).solve(0,3)
report={'bzantium_v2_counterexample':{
    'n':1500,'edges':edges,'source':0,'target':3,
    'expected_distance':.3,'actual_result':got,
    'note':'Only four vertices participate; 1500 vertices bypass upstream n<1000 Dijkstra fallback.'}}

data=json.loads((ROOT/'data/real_10000.json').read_text())
g=Graph(data['n'])
for edge in data['edges']: g.add_edge(*edge)
calls=[]
original=BmsspSolver._base_case
def record(self,bound,frontier,goal):
    result=original(self,bound,frontier,goal)
    calls.append({'bound':str(bound),'sources':len(frontier),'completed':len(result)})
    return result
BmsspSolver._base_case=record
solver=BmsspSolver(g)
solver.solve(0,data['n']-1)
report['bzantium_v1_base_calls']=calls

mod=load_module('nobelk_audit',ROOT/'repos/python_reference/src/sssp/bmssp.py')
g=mod.Graph(data['n'])
for edge in data['edges']: g.add_edge(*edge)
calls=[]
original_n=mod._base_case
def record_n(state,bound,sources):
    result=original_n(state,bound,sources)
    calls.append({'bound':str(bound),'sources':len(sources),'completed':len(result[1]),'k':state.k})
    return result
mod._base_case=record_n
mod.sssp(g,0)
report['nobelk_base_calls']=calls
(ROOT/'audit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
