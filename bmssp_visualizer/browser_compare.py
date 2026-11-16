"""Same-input 100-node Dijkstra/BMSSP UI validation and exported recordings."""
import json
from pathlib import Path
import time
import urllib.request
from browser_check import Browser,ROOT

OUT=ROOT/'exports'/'weighted_100'
OUT.mkdir(parents=True,exist_ok=True)
report={'runs':{}}
b=Browser('http://127.0.0.1:8765/?dataset=weighted_100')
try:
    b.wait("document.getElementById('stat-n').textContent==='100'")
    assert b.evaluate("document.querySelectorAll('#graph-svg [data-node]').length===100")
    assert b.evaluate("getComputedStyle(document.getElementById('graph-svg')).display!=='none'")
    b.screenshot(OUT/'input.png')
    b.evaluate("window._blobs=[];window._originalURL=URL.createObjectURL;URL.createObjectURL=blob=>{window._blobs.push(blob);return window._originalURL(blob);};window._fetch=window.fetch;window.fetch=async(...a)=>{const r=await window._fetch(...a);if(a[0]==='/api/runs'&&a[1]?.method==='POST')window._runId=(await r.clone().json()).run_id;return r;}")
    b.evaluate("document.querySelector('[data-export=svg]').click()")
    b.wait('window._blobs.length>0')
    (OUT/'input.svg').write_text(b.evaluate('window._blobs.at(-1).text()'),encoding='utf-8')

    def state():
        rid=b.evaluate('window._runId')
        with urllib.request.urlopen(f'http://127.0.0.1:8765/api/runs/{rid}') as f:
            return json.load(f)

    def run(algorithm,controlled=False):
        previous=b.evaluate('window._runId')
        button='run-dijkstra' if algorithm=='dijkstra' else 'run'
        mode='controlled' if controlled else 'fast'
        b.evaluate(f"document.getElementById('mode').value='{mode}';document.getElementById('mode').dispatchEvent(new Event('change'));document.getElementById('speed').value={120 if controlled else 0};document.getElementById('{button}').click()")
        b.wait(f"window._runId && window._runId!=={json.dumps(previous)}")
        if controlled:
            b.wait("Number(document.getElementById('event-count').textContent)>3")
            b.evaluate("document.getElementById('pause').click()")
            b.wait("!document.getElementById('step').disabled")
            time.sleep(.15)
            count=state()['events_count']
            time.sleep(.2)
            assert state()['events_count']==count
            b.evaluate("document.getElementById('step').click()")
            b.wait(f"Number(document.getElementById('event-count').textContent)>{count}")
            time.sleep(.15)
            assert state()['status']=='paused'
            b.evaluate("document.getElementById('speed').value=0;document.getElementById('speed').dispatchEvent(new Event('change'));document.getElementById('pause').click()")
        b.wait("document.getElementById('run-status').textContent==='Hoàn tất'",30)
        result=state()['result']
        assert result['algorithm_id']==algorithm and result['verified']
        assert result['distances'][99]==93
        assert b.evaluate("document.getElementById('path-cost').textContent==='93'")
        return result

    for algorithm in ('dijkstra','bmssp'):
        result=run(algorithm)
        report['runs'][algorithm]=result
    assert report['runs']['dijkstra']['distances']==report['runs']['bmssp']['distances']
    assert report['runs']['dijkstra']['graph_hash']==report['runs']['bmssp']['graph_hash']
    report['same_input_same_distances']=True
    assert b.evaluate("[...document.querySelectorAll('#comparison-body tr')].every(r=>r.textContent.includes('Khớp'))")
    b.evaluate("document.querySelector('.comparison-panel').scrollIntoView({block:'center'})")
    b.screenshot(OUT/'comparison.png')
    for algorithm in ('dijkstra','bmssp'):
        result=run(algorithm,True)
        rid=b.evaluate('window._runId')
        for kind,filename in [('graph','graph.json'),('result',algorithm+'-result.json'),('trace',algorithm+'-trace.ndjson'),('csv',algorithm+'-distances.csv')]:
            with urllib.request.urlopen(f'http://127.0.0.1:8765/api/runs/{rid}/export?kind={kind}') as f:
                (OUT/filename).write_bytes(f.read())
        b.evaluate("document.querySelector('[data-view=path]').click();document.getElementById('graph-stage').scrollIntoView({block:'center'})")
        b.screenshot(OUT/(algorithm+'-output.png'))
        count=b.evaluate('window._blobs.length')
        b.evaluate("document.querySelector('[data-export=html]').click()")
        b.wait(f'window._blobs.length>{count}')
        (OUT/(algorithm+'-demo.html')).write_text(b.evaluate('window._blobs.at(-1).text()'),encoding='utf-8')
        count=b.evaluate('window._blobs.length')
        b.evaluate("document.querySelector('[data-export=svg]').click()")
        b.wait(f'window._blobs.length>{count}')
        (OUT/(algorithm+'-output.svg')).write_text(b.evaluate('window._blobs.at(-1).text()'),encoding='utf-8')
        report[algorithm+'_controlled']=True
    (OUT/'comparison-fast.json').write_text(json.dumps(report['runs'],ensure_ascii=False,indent=2),encoding='utf-8')
    # Source change must invalidate both historical measurements before leaving
    # this live page; the fetch observer is intentionally page-scoped.
    b.evaluate("document.getElementById('source').value=5;document.getElementById('source').dispatchEvent(new Event('change'))")
    assert b.evaluate("[...document.querySelectorAll('#comparison-body tr')].every(r=>r.textContent.includes('Chưa chạy'))")
    report['source_invalidation']=True
    # Saved comparison belongs to this exact graph/source and survives offline reopen.
    for algorithm in ('dijkstra','bmssp'):
        b.call('Page.navigate',{'url':(OUT/(algorithm+'-demo.html')).as_uri()})
        b.wait("document.getElementById('connection-label').textContent.includes('Ngoại tuyến')")
        b.wait("[...document.querySelectorAll('#comparison-body tr')].every(r=>r.textContent.includes('Khớp'))")
        button='run-dijkstra' if algorithm=='dijkstra' else 'run'
        assert b.evaluate(f"!document.getElementById('{button}').hidden")
        b.evaluate(f"document.getElementById('speed').value=0;document.getElementById('{button}').click()")
        b.wait("document.getElementById('run-status').textContent.includes('Bản ghi')",45)
        assert b.evaluate("document.getElementById('path-cost').textContent==='93'")
        report[algorithm+'_offline_replay']=True
    report['js_errors']=b.errors
    assert not b.errors
    print(json.dumps({k:v for k,v in report.items() if k!='runs'},ensure_ascii=False))
finally:
    (ROOT/'verification_compare.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    b.close()
