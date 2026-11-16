"""End-to-end browser checks and reviewable demo artifacts."""
import json
from pathlib import Path
import time
import urllib.request
from browser_check import Browser, ROOT

OUT=ROOT/'exports'/'demo'
OUT.mkdir(parents=True,exist_ok=True)
report={}
b=Browser()
try:
    b.wait("document.getElementById('stat-n').textContent==='18'")
    report['input']={'nodes':b.evaluate("document.querySelectorAll('[data-node]').length"),
                     'edges':b.evaluate("document.getElementById('stat-m').textContent")}
    b.screenshot(OUT/'input.png')
    b.evaluate("window._inputBlob=null;window._inputURL=URL.createObjectURL;URL.createObjectURL=blob=>{window._inputBlob=blob;return window._inputURL(blob);};document.querySelector('[data-export=svg]').click()")
    b.wait('window._inputBlob!==null')
    (OUT/'input.svg').write_text(b.evaluate('window._inputBlob.text()'),encoding='utf-8')
    # Observe POST response without altering the app's own fetch semantics.
    b.evaluate("""window._fetch=window.fetch;window.fetch=async(...a)=>{const r=await window._fetch(...a);if(a[0]==='/api/runs'&&a[1]?.method==='POST'){window._lastRun=(await r.clone().json()).run_id;}return r;};document.getElementById('speed').value=180;document.getElementById('speed').dispatchEvent(new Event('input'));document.getElementById('run').click();""")
    b.wait("window._lastRun && Number(document.getElementById('event-count').textContent)>=3")
    b.evaluate("document.getElementById('pause').click()")
    b.wait("!document.getElementById('step').disabled")
    rid=b.evaluate('window._lastRun')
    time.sleep(.3)
    def snapshot():
        with urllib.request.urlopen(f'http://127.0.0.1:8765/api/runs/{rid}') as f:
            return json.load(f)
    before=snapshot()['events_count']
    time.sleep(.4)
    assert snapshot()['events_count']==before,'Runner advanced while paused'
    b.evaluate("document.getElementById('step').click()")
    b.wait(f"Number(document.getElementById('event-count').textContent)>{before}")
    time.sleep(.2)
    after=snapshot()['events_count']
    time.sleep(.3)
    assert snapshot()['events_count']==after,'Step did not return to pause'
    report['controlled']={'paused_stable':True,'one_step_returns_to_pause':True,
                          'before':before,'after':after,'run_id':rid}
    b.screenshot(OUT/'live.png')
    b.evaluate("document.getElementById('speed').value=0;document.getElementById('speed').dispatchEvent(new Event('change'));document.getElementById('pause').click()")
    b.wait("document.getElementById('verification').textContent.includes('Khớp')",20)
    completed=snapshot()
    assert completed['result']['verified']
    assert completed['result']['distances'][16]==11
    assert completed['result']['distances'][17] is None
    report['result']=completed['result']
    b.evaluate("document.querySelector('[data-view=path]').click()")
    b.wait("document.getElementById('path-cost').textContent==='11'")
    b.screenshot(OUT/'output.png')
    # Save actual exports using the backend, then check both backend-generated
    # and browser-generated HTML. None of these are manually mocked results.
    for kind,filename in [('graph','graph.json'),('result','result.json'),('trace','trace.ndjson'),('csv','distances.csv'),('html','demo.html')]:
        with urllib.request.urlopen(f'http://127.0.0.1:8765/api/runs/{rid}/export?kind={kind}') as f:
            (OUT/filename).write_bytes(f.read())
    # Capture client-side download content (Blob) for standalone HTML and SVG,
    # without dependence on the host download dialog.
    b.evaluate("window._blobs=[];window._createURL=URL.createObjectURL;URL.createObjectURL=blob=>{window._blobs.push(blob);return window._createURL(blob);};document.querySelector('[data-export=html]').click()")
    b.wait('window._blobs.length>0')
    html=b.evaluate('window._blobs[window._blobs.length-1].text()')
    (OUT/'demo-browser.html').write_text(html,encoding='utf-8')
    b.evaluate("document.querySelector('[data-export=svg]').click()")
    b.wait('window._blobs.length>1')
    svg=b.evaluate('window._blobs[window._blobs.length-1].text()')
    assert '<svg' in svg
    (OUT/'output.svg').write_text(svg,encoding='utf-8')
    # Reopen HTML directly from disk: no server is needed for replay.
    b.call('Page.navigate',{'url':(OUT/'demo-browser.html').as_uri()})
    b.wait("document.getElementById('connection-label')?.textContent.includes('Ngoại tuyến')")
    b.wait("document.getElementById('verification').textContent.includes('Khớp')")
    report['offline_client_html']=True
    b.evaluate("document.getElementById('speed').value=0;document.getElementById('run').click()")
    b.wait("document.getElementById('run-status').textContent.includes('Bản ghi')",20)
    assert b.evaluate("document.getElementById('verification').textContent.includes('Khớp')")
    report['offline_replay_completed']=True
    b.screenshot(OUT/'offline.png')
    b.call('Page.navigate',{'url':(OUT/'demo.html').as_uri()})
    b.wait("document.getElementById('connection-label')?.textContent.includes('Ngoại tuyến')")
    b.wait("document.getElementById('verification').textContent.includes('Khớp')")
    report['offline_backend_html']=True
    # Mobile layout must not introduce horizontal page overflow.
    b.call('Emulation.setDeviceMetricsOverride',{'width':390,'height':844,'deviceScaleFactor':1,'mobile':True})
    b.evaluate('window.dispatchEvent(new Event("resize"))')
    time.sleep(.3)
    report['mobile_overflow']=b.evaluate('document.documentElement.scrollWidth>window.innerWidth+1')
    assert not report['mobile_overflow']
    b.screenshot(OUT/'mobile.png')
    report['js_errors']=b.errors
    assert not b.errors
    print(json.dumps({'input':report['input'],'controlled':report['controlled'],
                      'offline_client_html':True,'offline_backend_html':True,
                      'offline_replay_completed':True,'mobile_overflow':False,'js_errors':len(b.errors)},ensure_ascii=False))
finally:
    (ROOT/'verification_browser.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
    b.close()
