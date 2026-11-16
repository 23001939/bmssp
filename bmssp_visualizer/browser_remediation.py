"""Browser integration against an isolated server; versioned review evidence."""
import json, subprocess, sys, time, urllib.request
from pathlib import Path
from browser_check import Browser,ROOT

def main():
    out=ROOT/'exports'/'remediation';out.mkdir(exist_ok=True)
    server=subprocess.Popen([sys.executable,'-X','utf8',str(ROOT/'server.py'),'--port','8877'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    b=None;report={'runs':{}}
    try:
        for _ in range(100):
            try:
                urllib.request.urlopen('http://127.0.0.1:8877/api/datasets',timeout=1).close();break
            except Exception:time.sleep(.1)
        b=Browser('http://127.0.0.1:8877/?dataset=weighted_100',port=9230)
        b.wait("document.getElementById('stat-n').textContent==='100'")
        assert b.evaluate("getComputedStyle(document.getElementById('graph-svg')).display!=='none'")
        b.evaluate("window._fetch=window.fetch;window.fetch=async(...args)=>{const r=await window._fetch(...args);if(args[0]==='/api/runs'&&args[1]?.method==='POST')window._runId=(await r.clone().json()).run_id;return r;}")
        for algo in ['bmssp','dijkstra']:
            b.evaluate("window._runId=null;document.getElementById('mode').value='fast';document.getElementById('mode').dispatchEvent(new Event('change'));")
            b.evaluate(f"document.getElementById('{ 'run' if algo=='bmssp' else 'run-dijkstra'}').click()")
            b.wait('!!window._runId');rid=b.evaluate('window._runId')
            for _ in range(200):
                with urllib.request.urlopen(f'http://127.0.0.1:8877/api/runs/{rid}') as f:r=json.load(f)
                if r.get('result'):break
                time.sleep(.05)
            result=r['result'];assert result['verified'];assert result['verification']['path_exact_ok'];assert result['verification']['comparison_mode']=='exact'
            report['runs'][algo]={'run_id':rid,'graph_hash':result['graph_hash'],'verification':result['verification']}
            for kind in ['result','html','csv']:
                with urllib.request.urlopen(f'http://127.0.0.1:8877/api/runs/{rid}/export?kind={kind}') as f:data=f.read()
                (out/f'{algo}.{ {"result":"json","html":"html","csv":"csv"}[kind]}').write_bytes(data)
        assert report['runs']['bmssp']['graph_hash']==report['runs']['dijkstra']['graph_hash']
        b.wait("document.getElementById('verification-note').textContent.includes('chính xác')")
        b.screenshot(out/'comparison.png')
        b.call('Page.navigate',{'url':(out/'bmssp.html').as_uri()})
        b.wait("document.readyState==='complete' && document.getElementById('stat-n').textContent==='100'")
        assert b.evaluate("document.getElementById('verification-note').textContent.includes('chính xác')")
        report['offline_html']=True;report['browser_errors']=b.errors
        assert not b.errors
        (out/'browser.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        print(out/'browser.json')
    finally:
        if b:b.close()
        server.terminate();server.wait(timeout=5)
if __name__=='__main__':main()
