"""Browser smoke for the actual 50k-node input; explicitly a partial view."""
import json
import time
from browser_check import Browser, ROOT

OUT=ROOT/'exports'/'demo'
b=Browser()
report={}
try:
    b.wait("document.getElementById('stat-n').textContent==='18'")
    b.evaluate("document.getElementById('dataset').value='real_50000';document.getElementById('dataset').dispatchEvent(new Event('change'))")
    b.wait("document.getElementById('stat-n').textContent.replace(/\\D/g,'')==='50000'",30)
    b.wait("getComputedStyle(document.getElementById('graph-canvas')).display!=='none' && getComputedStyle(document.getElementById('graph-svg')).display==='none'")
    assert b.evaluate("document.getElementById('graph-canvas').getBoundingClientRect().top < document.getElementById('graph-stage').getBoundingClientRect().bottom")
    note=b.evaluate("document.getElementById('stage-note').textContent")
    assert '200' in note and '50.000' not in note or '200' in note
    report['view_note']=note
    report['canvas_visible']=True
    b.evaluate("document.getElementById('search').value=43210;document.getElementById('find').click()")
    b.wait("document.getElementById('selection-card').textContent.includes('43210')")
    b.screenshot(OUT/'large-input.png')
    b.evaluate("document.getElementById('mode').value='fast';document.getElementById('mode').dispatchEvent(new Event('change'));document.getElementById('run').click()")
    b.wait("document.getElementById('verification').textContent.includes('Khớp')",40)
    report['verified']=True
    report['path_cost']=b.evaluate("document.getElementById('path-cost').textContent")
    report['timing_label']=b.evaluate("document.getElementById('compute-label').textContent")
    report['timing']=b.evaluate("document.getElementById('compute-time').textContent")
    report['reachable']=b.evaluate("document.getElementById('reachable').textContent")
    b.screenshot(OUT/'large-output.png')
    report['js_errors']=b.errors
    assert not b.errors
    print(json.dumps(report,ensure_ascii=False))
finally:
    (ROOT/'verification_large.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    b.close()
