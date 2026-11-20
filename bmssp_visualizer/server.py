"""Local BMSSP workbench: stdlib HTTP, real native event streaming and exports."""
from __future__ import annotations

import argparse
import copy
import csv
import io
import hashlib
import json
import math
import mimetypes
from pathlib import Path
import subprocess
import sys
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

from data import demo_graph, weighted_graph_100, normalize_graph, parse_text, integer, verify_result
from build_native import ensure_built, runtime_environment

ROOT = Path(__file__).resolve().parent
DIST = ROOT/'dist'
INPUTS = ROOT/'data'
RUNS = ROOT/'runs'
TERMINAL = {'completed','error','stopped'}
MAX_EVENTS = 12_000
BUILD_LOCK = threading.Lock()


def encode(value):
    return json.dumps(value,ensure_ascii=False,allow_nan=False,separators=(',',':'))


class Run:
    def __init__(self, graph, source, mode, delay, algorithm='bmssp'):
        self.id = uuid.uuid4().hex[:16]
        self.graph = copy.deepcopy(graph)
        self.graph['source'] = source
        self.source, self.mode, self.delay_ms = source, mode, delay
        self.algorithm=algorithm
        self.graph_hash=hashlib.sha256(encode({'n':graph['n'],
            'edges':[[e['u'],e['v'],e['w']] for e in graph['edges']]}).encode('utf-8')).hexdigest()
        self.created=time.perf_counter()
        self.edge_lookup={}
        for edge in graph['edges']:
            key=(edge['u'],edge['v'])
            old=self.edge_lookup.get(key)
            if old is None or (edge['w'],edge['id']) < (old['w'],old['id']):
                self.edge_lookup[key]=edge
        self.status = 'running'
        self.cv = threading.Condition()
        self.result = None
        self.error = None
        self.events = []
        self.sampled = False
        self.dropped = 0
        self.step_budget = 0
        self.process = None
        self.finished = threading.Event()
        self.directory = RUNS/self.id
        self.directory.mkdir(parents=True,exist_ok=True)
        (self.directory/'graph.json').write_text(encode(self.graph),encoding='utf-8')
        self.input_path = self.directory/'input.txt'
        with self.input_path.open('w',encoding='utf-8') as f:
            f.write(f"{graph['n']} {len(graph['edges'])}\n")
            for e in graph['edges']:
                f.write(f"{e['u']} {e['v']} {e['w']:.17g}\n")

    def snapshot(self):
        with self.cv:
            return {'run_id':self.id,'status':self.status,'graph_id':self.graph['id'],
                    'source':self.source,'mode':self.mode,'algorithm':self.algorithm,'result':self.result,
                    'events_count':len(self.events),'sampled':self.sampled,
                    'dropped_events':self.dropped,'error':self.error}

    def emit(self, event, force=False):
        with self.cv:
            if len(self.events)>=MAX_EVENTS and not force:
                self.sampled=True
                self.dropped+=1
                return
            item={**event,'run_id':self.id,'seq':len(self.events)+1,
                  'elapsed_ms':(time.perf_counter()-self.created)*1000}
            self.events.append(item)
            with (self.directory/'trace.ndjson').open('a',encoding='utf-8') as f:
                f.write(encode(item)+'\n')
            self.cv.notify_all()

    def control(self, action, delay=None):
        if action not in ('pause','resume','step','stop','speed'):
            raise ValueError('Điều khiển không hợp lệ.')
        with self.cv:
            if delay is not None:
                self.delay_ms=integer(delay,'Nhịp demo',0,1000)
            if self.status in TERMINAL:
                return self.snapshot()
            if action=='stop':
                self.status='stopped'
                if self.process and self.process.poll() is None:
                    self.process.terminate()
                self.emit({'type':'stopped'},True)
            elif self.mode=='fast' and action!='speed':
                raise ValueError('Chế độ đo không trace chỉ hỗ trợ Dừng.')
            elif action=='pause':
                self.status='paused'
                self.step_budget=0
            elif action=='resume':
                self.status='running'
                self.step_budget=0
            elif action=='step':
                self.status='paused'
                self.step_budget+=1
            self.emit({'type':'status','status':self.status,'delay_ms':self.delay_ms},True)
            self.cv.notify_all()
            return self.snapshot()

    def allow_next(self):
        # No acknowledgement reaches C++ while paused. A step grants exactly one
        # next emission; the result terminal message itself never needs an ACK.
        with self.cv:
            while True:
                if self.status=='stopped':
                    return False
                if self.status=='paused':
                    if self.step_budget:
                        self.step_budget-=1
                        return True
                    self.cv.wait(.25)
                    continue
                delay=self.delay_ms/1000
                if delay:
                    changed=self.cv.wait(delay)
                    if changed:
                        continue
                return self.status!='stopped'

    def work(self):
        started=time.perf_counter()
        try:
            with BUILD_LOCK:
                if self.algorithm=='dijkstra':
                    from build_native import ensure_dijkstra_built
                    exe=ensure_dijkstra_built()
                else:
                    exe=ensure_built()
            if self.status=='stopped':
                return
            sample=1 if self.graph['n']<=150 else max(1,math.ceil(len(self.graph['edges'])*8/2500))
            self.sampled=self.mode=='controlled' and sample>1
            if self.mode=='fast':
                self.emit({'type':'start','n':self.graph['n'],'source':self.source,
                           'algorithm':('Dijkstra · binary heap' if self.algorithm=='dijkstra' else 'BMSSP WC · không trace'),
                           'algorithm_id':self.algorithm,'sampled':False},True)
            native_mode='trace' if self.mode=='controlled' else 'plain'
            self.process=subprocess.Popen([str(exe),str(self.input_path),str(self.source),native_mode,str(sample)],
                stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                text=True,encoding='utf-8',bufsize=1,env=runtime_environment(exe))
            native=None
            for line in self.process.stdout:
                if self.status=='stopped':
                    break
                event=json.loads(line)
                if event.get('type')=='native_result':
                    native=event
                    break
                if event.get('type')=='relax':
                    idx=event.get('edge_id')
                    edge=(self.graph['edges'][idx] if self.algorithm=='dijkstra' and isinstance(idx,int)
                          and 0<=idx<len(self.graph['edges']) else
                          self.edge_lookup.get((event.get('u'),event.get('v'))))
                    if edge:
                        event['edge_id']=edge['id']
                self.emit(event)
                if not self.allow_next():
                    break
                self.process.stdin.write('next\n')
                self.process.stdin.flush()
            if self.status=='stopped':
                return
            self.process.stdin.close()
            code=self.process.wait(timeout=15)
            stderr=self.process.stderr.read()
            if code or native is None:
                raise RuntimeError(stderr.strip() or f'Runner kết thúc không có kết quả (mã {code}).')
            native.pop('type',None)
            self.sampled=self.sampled or bool(native.get('sampled'))
            result={**native,'algorithm':('Dijkstra · binary heap' if self.algorithm=='dijkstra' else 'BMSSP WC · bản demo sửa so sánh số thực'),
                    'algorithm_id':self.algorithm,'source':self.source,
                    'graph_hash':self.graph_hash,
                    'native_compute_ms':native.get('compute_ms'),
                    'predecessor_method':('Parents recorded by native Dijkstra edge relaxations' if self.algorithm=='dijkstra'
                                          else native.get('predecessor_origin','unspecified')),
                    'predecessor_origin':native.get('predecessor_origin','solver' if self.algorithm=='dijkstra' else 'unspecified'),
                    'reachable':[x is not None for x in native['distances']],
                    'wall_ms':(time.perf_counter()-started)*1000,
                    'mode':self.mode,'baseline_ms':None,
                    'timing_note':'Thời gian runner gồm overhead thu sự kiện; thời gian chờ điều khiển được tách riêng.' if self.mode=='controlled'
                                  else ('Dijkstra C++ không trace, một lần chạy riêng; không phải trung vị benchmark.' if self.algorithm=='dijkstra'
                                        else 'Lần chạy native không trace; xem baseline_ms cho phép đo upstream nguyên bản riêng.'),
                    'trace':{'sampled':self.sampled,'events':len(self.events),'sample_every':sample,
                             'dropped_events':self.dropped},
                    'provenance':{'repo':'https://github.com/lcs147/bmssp',
                                  'commit':'0a8eb82805bad7f64dfa1b51e17656c9af4c4681',
                                  'instrumented_copy':True,'degree_transformation':False,
                                  'patches':native.get('patches',[])}}
            if self.algorithm=='dijkstra':
                result['provenance']={'implementation':'Native C++ binary-heap Dijkstra',
                                      'source_file':'native/dijkstra.cpp','instrumented_copy':False,
                                      'comparison_rounding':False}
            check=verify_result(self.graph,self.source,result)
            result['verification']=check
            result['verified']=check['ok']
            # A second process includes no tracing hooks and uses the original
            # checkout. Its duration is never animation or pause time.
            if self.mode=='fast' and self.algorithm=='bmssp':
                try:
                    from build_native import ensure_baseline_built
                    with BUILD_LOCK:
                        baseline=ensure_baseline_built()
                    clean=subprocess.run([str(baseline),str(self.input_path),str(self.source)],
                                         capture_output=True,text=True,encoding='utf-8',timeout=30,check=True,
                                         env=runtime_environment(baseline))
                    measurement=json.loads(clean.stdout.strip().splitlines()[-1])
                    baseline_result={'distances':measurement['distances'],
                                     'predecessors':result['predecessors']}
                    same=verify_result(self.graph,self.source,baseline_result)['ok']
                    result['baseline_ms']=measurement['compute_ms']
                    result['baseline_verified']=same
                    result['baseline_verification']=verify_result(self.graph,self.source,baseline_result)
                    result['baseline_note']='Một lần chạy riêng của upstream nguyên bản, không hook; không phải trung vị benchmark.'
                except Exception as exc:
                    result['baseline_error']=str(exc)
            result['wall_ms']=(time.perf_counter()-started)*1000
            with self.cv:
                if self.status=='stopped':
                    return
                self.result=result
                self.status='completed'
                (self.directory/'result.json').write_text(encode(result),encoding='utf-8')
                self.emit({'type':'result','result':result},True)
        except Exception as exc:
            with self.cv:
                if self.status!='stopped':
                    self.error=str(exc)
                    self.status='error'
                    self.emit({'type':'error','error':str(exc)},True)
        finally:
            if self.process:
                if self.process.poll() is None:
                    self.process.terminate()
                try:
                    self.process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
                for pipe in (self.process.stdin,self.process.stdout,self.process.stderr):
                    if pipe and not pipe.closed:
                        pipe.close()
            self.finished.set()
            with self.cv:
                self.cv.notify_all()


class App:
    def __init__(self):
        self.graphs={'demo':demo_graph(),'weighted_100':weighted_graph_100()}
        self.runs={}
        self.lock=threading.RLock()
        self.catalog=[{'id':'demo','name':self.graphs['demo']['name'],'n':18,'m':30,
                       'description':'Bộ nhỏ để quan sát đầy đủ từng bước; có đỉnh không tới được.'},
                      {'id':'weighted_100','name':self.graphs['weighted_100']['name'],'n':100,'m':240,
                       'description':self.graphs['weighted_100']['metadata']['description']}]
        for path in sorted(INPUTS.glob('*.json')):
            # Dataset descriptors from the existing pinned report, load actual
            # graph only when selected to avoid eager 50k-node copies.
            stem=path.stem
            sizes={'counterexample_1500':(1500,1501),'real_2000':(2000,7996),
                   'real_10000':(10000,39996),'real_50000':(50000,199996),
                   'integer_10000':(10000,39996),'zero_ties_10000':(10000,39996),
                   'USA-road-t.NY':(264346,733846),'USA-road-t.BAY':(321270,800172),'USA-road-t.COL':(435666,1057066)}
            if stem in sizes:
                n,m=sizes[stem]
                desc=('Mạng lưới đường bộ thực tế (DIMACS-9) dùng trong benchmark.'
                      if 'USA-road' in stem else
                      'Dữ liệu tổng hợp đã dùng trong báo cáo benchmark.')
                self.catalog.append({'id':stem,'name':stem,'n':n,'m':m,
                                     'description':desc})

    def graph(self, gid):
        with self.lock:
            if gid not in self.graphs:
                if gid not in {x['id'] for x in self.catalog}:
                    raise KeyError('Không tìm thấy đồ thị.')
                raw=json.loads((INPUTS/(gid+'.json')).read_text(encoding='utf-8'))
                raw['id']=gid
                self.graphs[gid]=normalize_graph(raw,gid)
            return self.graphs[gid]

    def upload(self, payload):
        if 'graph' in payload:
            graph=normalize_graph(payload['graph'],payload.get('name'))
        else:
            graph=parse_text(payload.get('text'),payload.get('format','txt'),payload.get('name'))
        graph['id']='upload_'+uuid.uuid4().hex[:12]
        with self.lock:
            self.graphs[graph['id']]=graph
        return graph

    def start(self, payload):
        graph=self.graph(payload.get('graph_id','demo'))
        source=integer(payload.get('source',graph['source']),'Đỉnh nguồn',0,graph['n']-1)
        mode=payload.get('mode','controlled')
        if mode not in ('controlled','fast'):
            raise ValueError('Chế độ chạy không hợp lệ.')
        algorithm=payload.get('algorithm','bmssp')
        if algorithm not in ('bmssp','dijkstra'):
            raise ValueError('Thuật toán phải là bmssp hoặc dijkstra.')
        delay=integer(payload.get('delay_ms',100),'Nhịp demo',0,1000)
        with self.lock:
            if any(r.status not in TERMINAL for r in self.runs.values()):
                raise BlockingIOError('Một thuật toán đang chạy. Hãy Dừng trước khi tạo lần chạy mới.')
            run=Run(graph,source,mode,delay,algorithm)
            self.runs[run.id]=run
            threading.Thread(target=run.work,daemon=True,name='bmssp-'+run.id).start()
            return run

    def get_run(self, rid):
        with self.lock:
            if rid not in self.runs:
                raise KeyError('Không tìm thấy lần chạy.')
            return self.runs[rid]

    def close(self):
        with self.lock:
            runs=list(self.runs.values())
        for run in runs:
            if run.status not in TERMINAL:
                run.control('stop')
        for run in runs:
            run.finished.wait(3)


def export_html(run):
    template=(DIST/'index.html').read_text(encoding='utf-8')
    css=(DIST/'style.css').read_text(encoding='utf-8')
    js=(DIST/'app.js').read_text(encoding='utf-8')
    payload=encode({'graph':run.graph,'result':run.result,'events':run.events})
    # Escape all embedded HTML-sensitive characters, including malicious graph
    # names; payload remains valid JSON when parsed from textContent.
    payload=payload.replace('&','\\u0026').replace('<','\\u003c').replace('>','\\u003e')
    import re
    template=re.sub(r'<link\b[^>]*href=["\'][^"\']*style\.css[^"\']*["\'][^>]*>',lambda _: '<style>'+css+'</style>',template)
    template=re.sub(r'<script\b[^>]*src=["\'][^"\']*app\.js[^"\']*["\'][^>]*>\s*</script>', '',template)
    script='<script id="offline-data" type="application/json">'+payload+'</script>\n<script data-app="true">'+js.replace('</script','<\\/script')+'</script>'
    return template.replace('</body>',script+'\n</body>')


class Handler(BaseHTTPRequestHandler):
    protocol_version='HTTP/1.1'

    def handle(self):
        try:
            super().handle()
        except (ConnectionResetError,ConnectionAbortedError,BrokenPipeError):
            pass

    def log_message(self, fmt, *args):
        if self.server.verbose:
            super().log_message(fmt,*args)

    @property
    def app(self):
        return self.server.app

    def respond(self, value, status=200, content_type='application/json; charset=utf-8', filename=None):
        raw=value if isinstance(value,bytes) else (value if isinstance(value,str) else encode(value)).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type',content_type)
        self.send_header('Content-Length',str(len(raw)))
        self.send_header('Cache-Control','no-store')
        self.send_header('X-Content-Type-Options','nosniff')
        if filename:
            self.send_header('Content-Disposition',f'attachment; filename="{filename}"')
        self.end_headers()
        self.wfile.write(raw)

    def payload(self):
        size=int(self.headers.get('Content-Length','0'))
        if size < 1 or size > 40*1024*1024:
            raise ValueError('Dữ liệu yêu cầu trống hoặc vượt 40 MB.')
        obj=json.loads(self.rfile.read(size).decode('utf-8'))
        if not isinstance(obj,dict):
            raise ValueError('Yêu cầu JSON phải là một object.')
        return obj

    def do_GET(self):
        self.dispatch(False)

    def do_POST(self):
        self.dispatch(True)

    def dispatch(self, post):
        try:
            parsed=urlsplit(self.path)
            path=parsed.path
            query=parse_qs(parsed.query)
            if path=='/api/datasets' and not post:
                return self.respond({'datasets':self.app.catalog})
            if path=='/api/graph' and not post:
                return self.respond(self.app.graph(query.get('id',['demo'])[0]))
            if path=='/api/graphs' and post:
                graph=self.app.upload(self.payload())
                return self.respond({'id':graph['id'],'graph':graph})
            if path=='/api/runs' and post:
                run=self.app.start(self.payload())
                return self.respond({'run_id':run.id,'status':run.status,'algorithm':run.algorithm})
            if path.startswith('/api/runs/'):
                parts=path.strip('/').split('/')
                run=self.app.get_run(parts[2])
                if len(parts)==3 and not post:
                    return self.respond(run.snapshot())
                action=parts[3] if len(parts)==4 else ''
                if action=='control' and post:
                    payload=self.payload()
                    return self.respond(run.control(payload.get('action'),payload.get('delay_ms')))
                if action=='events' and not post:
                    return self.stream(run,int(query.get('after',['0'])[0]))
                if action=='export' and not post:
                    return self.download(run,query.get('kind',['result'])[0])
            if not post and path in ('/','/index.html','/app.js','/style.css'):
                file=DIST/('index.html' if path=='/' else path[1:])
                if not file.exists():
                    return self.respond({'error':'Giao diện chưa sẵn sàng.'},503)
                return self.respond(file.read_bytes(),content_type=mimetypes.guess_type(file.name)[0]+'; charset=utf-8')
            return self.respond({'error':'Không tìm thấy tài nguyên.'},404)
        except BlockingIOError as exc:
            self.respond({'error':str(exc)},409)
        except KeyError as exc:
            self.respond({'error':str(exc)},404)
        except (ValueError,TypeError,OverflowError) as exc:
            self.respond({'error':str(exc)},400)
        except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):
            pass
        except Exception as exc:
            self.respond({'error':str(exc)},500)

    def stream(self, run, after):
        self.send_response(200)
        self.send_header('Content-Type','text/event-stream; charset=utf-8')
        self.send_header('Cache-Control','no-cache')
        self.send_header('Connection','close')
        self.end_headers()
        self.close_connection=True
        cursor=max(0,after)
        while True:
            with run.cv:
                pending=[e for e in run.events if e['seq']>cursor]
                done=run.status in TERMINAL
                if not pending and not done:
                    run.cv.wait(1)
                    pending=[e for e in run.events if e['seq']>cursor]
                    done=run.status in TERMINAL
            if pending:
                for event in pending:
                    self.wfile.write(('data: '+encode(event)+'\n\n').encode('utf-8'))
                    cursor=event['seq']
            elif done:
                break
            else:
                self.wfile.write(b': heartbeat\n\n')
            self.wfile.flush()

    def download(self, run, kind):
        if kind=='graph':
            return self.respond(run.graph,filename='graph.json')
        if kind=='trace':
            return self.respond(''.join(encode(e)+'\n' for e in run.events),
                                content_type='application/x-ndjson; charset=utf-8',filename='trace.ndjson')
        if run.result is None:
            raise ValueError('Lần chạy chưa có kết quả. Có thể tải đồ thị hoặc trace hiện tại.')
        if kind=='result':
            return self.respond(run.result,filename='result.json')
        if kind=='csv':
            text=io.StringIO(newline='')
            writer=csv.writer(text)
            writer.writerow(['vertex','distance','predecessor','predecessor_edge','reachable'])
            r=run.result
            for v in range(run.graph['n']):
                writer.writerow([v,r['distances'][v],r['predecessors'][v],r['predecessor_edges'][v],r['reachable'][v]])
            return self.respond(text.getvalue(),content_type='text/csv; charset=utf-8',filename='distances.csv')
        if kind=='html':
            return self.respond(export_html(run),content_type='text/html; charset=utf-8',filename='demo.html')
        raise ValueError('Loại file xuất không hợp lệ.')


class LocalServer(ThreadingHTTPServer):
    daemon_threads=True

    def server_close(self):
        self.app.close()
        super().server_close()


def create_server(host='127.0.0.1',port=8765,verbose=False):
    server=LocalServer((host,port),Handler)
    server.app=App()
    server.verbose=verbose
    return server


def main():
    parser=argparse.ArgumentParser(description='BMSSP local visualizer')
    parser.add_argument('--port',type=int,default=8765)
    parser.add_argument('--verbose',action='store_true')
    args=parser.parse_args()
    print('Preparing C++ runner...',flush=True)
    ensure_built()
    server=create_server(port=args.port,verbose=args.verbose)
    print(f'BMSSP Visualizer: http://127.0.0.1:{server.server_address[1]}',flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__=='__main__':
    main()
