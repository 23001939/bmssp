"""Verify UI file imports and genuine live autoplay using installed Chrome."""
import json
from browser_check import Browser,ROOT

out=ROOT/'exports'/'demo'
b=Browser((out/'demo.html').as_uri())
report={}
try:
    b.wait("document.getElementById('verification').textContent.includes('Khớp')")
    def upload(name):
        doc=b.call('DOM.getDocument')
        node=b.call('DOM.querySelector',{'nodeId':doc['root']['nodeId'],'selector':'#file-input'})
        b.call('DOM.setFileInputFiles',{'nodeId':node['nodeId'],'files':[str(out/name)]})
    upload('graph.json')
    b.wait("document.getElementById('verification').textContent==='Chưa chạy'")
    upload('result.json')
    b.wait("document.getElementById('verification').textContent.includes('Khớp')")
    assert b.evaluate("document.getElementById('path-cost').textContent==='11'")
    report['graph_then_result_import']=True
    upload('trace.ndjson')
    b.wait("Number(document.getElementById('event-count').textContent)>0")
    report['trace_ndjson_import']=True
    b.call('Page.navigate',{'url':'http://127.0.0.1:8765/?demo=live'})
    b.wait("document.getElementById('run-status').textContent.includes('Đang chạy')")
    b.wait("Number(document.getElementById('event-count').textContent)>3")
    report['autoplay_is_live']=True
    b.evaluate("document.getElementById('stop').click()")
    b.wait("document.getElementById('run-status').textContent==='Đã dừng'")
    report['live_stop']=True
    report['js_errors']=b.errors
    assert not b.errors
    print(json.dumps(report,ensure_ascii=False))
finally:
    (ROOT/'verification_import.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    b.close()
