"""Regression: dataset/file changes must replace the visible renderer."""
import json
from pathlib import Path
from browser_check import Browser, ROOT

b=Browser()
report={}
try:
    b.wait("document.getElementById('stat-n').textContent==='18'")
    b.evaluate("document.getElementById('dataset').value='real_10000';document.getElementById('dataset').dispatchEvent(new Event('change'))")
    b.wait("document.getElementById('stat-n').textContent.replace(/\\D/g,'')==='10000'")
    report['large']=b.evaluate("""(()=>{const svg=document.getElementById('graph-svg'),c=document.getElementById('graph-canvas');return {svgDisplay:getComputedStyle(svg).display,svgHiddenAttribute:svg.hasAttribute('hidden'),svgHiddenProperty:svg.hidden,canvasDisplay:getComputedStyle(c).display,canvasTop:c.getBoundingClientRect().top,stageBottom:document.getElementById('graph-stage').getBoundingClientRect().bottom,canvasWidth:c.width,canvasHeight:c.height};})()""")
    assert report['large']['svgDisplay']=='none', f"Old SVG still visible: {report['large']}"
    assert report['large']['canvasDisplay']!='none'
    assert report['large']['canvasTop']<report['large']['stageBottom']
    assert b.evaluate("document.getElementById('graph-canvas').getContext('2d').getImageData(0,0,document.getElementById('graph-canvas').width,document.getElementById('graph-canvas').height).data.some(v=>v!==0)")
    b.screenshot(ROOT/'switch-large.png')
    b.evaluate("document.getElementById('dataset').value='demo';document.getElementById('dataset').dispatchEvent(new Event('change'))")
    b.wait("document.getElementById('stat-n').textContent==='18'")
    assert b.evaluate("getComputedStyle(document.getElementById('graph-svg')).display!=='none' && getComputedStyle(document.getElementById('graph-canvas')).display==='none'")
    report['small_again']=True
    temp=ROOT/'exports'/'switch-input.txt'
    temp.write_text('5 3\n0 4 3\n4 2 1\n2 3 2\n',encoding='utf-8')
    doc=b.call('DOM.getDocument')
    node=b.call('DOM.querySelector',{'nodeId':doc['root']['nodeId'],'selector':'#file-input'})
    b.call('DOM.setFileInputFiles',{'nodeId':node['nodeId'],'files':[str(temp)]})
    b.wait("document.getElementById('stat-n').textContent==='5'")
    b.wait("document.querySelectorAll('#graph-svg [data-node]').length===5")
    assert b.evaluate("document.querySelectorAll('#graph-svg [data-node=\"17\"]').length===0")
    report['txt_import_replaces_graph']=True
    b.screenshot(ROOT/'switch-import.png')
    # Upload actual large JSON through the file control as well as the dropdown.
    b.call('DOM.setFileInputFiles',{'nodeId':node['nodeId'],'files':[str(ROOT.parent/'bmssp_comparison/data/real_2000.json')]})
    b.wait("document.getElementById('stat-n').textContent.replace(/\\D/g,'')==='2000'")
    assert b.evaluate("getComputedStyle(document.getElementById('graph-svg')).display==='none' && getComputedStyle(document.getElementById('graph-canvas')).display!=='none'")
    report['large_json_import_replaces_graph']=True
    report['errors']=b.errors
    assert not b.errors
    print(json.dumps(report,ensure_ascii=False))
finally:
    (ROOT/'verification_switch.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    b.close()
