/* BMSSP Graph Lab — buildless, local-first; no simulated algorithm events. */
'use strict';
(() => {
  const $ = id => document.getElementById(id);
  const NS = 'http://www.w3.org/2000/svg';
  const fmt = n => n == null || !Number.isFinite(n) ? '∞' : Number(n.toPrecision(7)).toLocaleString('vi-VN', {maximumFractionDigits: 7});
  const integer = n => Number(n).toLocaleString('vi-VN');
  const S = {graph:null,result:null,events:[],runId:null,status:'idle',es:null,view:'graph',selected:null,dist:[],pred:[],predEdges:[],pivots:new Set(),complete:new Set(),frontier:new Set(),level:0,maxLevel:0,pivotCount:0,page:0,offline:false,replay:false,cursor:0,timer:null,startTime:0,wall:0,viewport:{x:0,y:0,scale:1},visible:[],edges:[],adj:[],edgeById:new Map(),activeEdge:null,busy:false,renderPending:false,move:null,assets:null,imported:false,recordedTraceInfo:null,algorithm:'dmm25',comparison:{dmm25:null,chd:null,dijkstra:null}};
  const colors = {source:'#127366',frontier:'#d8eaf3',pivot:'#eadcf6',complete:'#cde8d5',unseen:'#fff',path:'#e29b37'};
  let noticeTimer;
  function notice(message,error=false) { $('notice').textContent=message; $('notice').className='notice'+(error?' error':''); $('notice').hidden=false; clearTimeout(noticeTimer); noticeTimer=setTimeout(()=>$('notice').hidden=true,error?12000:6500); }
  async function api(path,data) { const r=await fetch(path,data===undefined?{}:{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)}); let value; try {value=await r.json();} catch {throw Error('Máy chủ trả dữ liệu không hợp lệ.');} if(!r.ok)throw Error(value.error||`Lỗi HTTP ${r.status}`); return value; }
  function safe(fn) { return async (...args) => {try {await fn(...args);} catch(e) {notice(e.message||String(e),true); S.busy=false; if(S.status==='starting')S.status='error'; updateControls();}}; }
  function el(tag,attrs={},content) { const node=document.createElementNS(NS,tag); for(const [key,value] of Object.entries(attrs))node.setAttribute(key,String(value)); if(content!==undefined)node.textContent=String(content); return node; }
  function buttonLock() {return S.busy || ['pending','running','paused','starting'].includes(S.status);}
  const ALGORITHM_IDS = new Set(['dmm25','chd','dijkstra']);
  function algorithmId(value) {
    const id = String(value?.algorithm_id || value?.algorithm || '').toLowerCase();
    if(id.includes('dijkstra')) return 'dijkstra';
    if(id.includes('chd')) return 'chd';
    // Accept recordings made by the former two-algorithm frontend.
    if(id === 'bmssp' || id.includes('dmm25') || id.includes('bmssp')) return 'dmm25';
    return 'dmm25';
  }
  function requestedAlgorithm(value) {
    const id = String(value || '').toLowerCase();
    if(id === 'bmssp') return 'dmm25';
    if(!ALGORITHM_IDS.has(id)) throw Error('Thuật toán không hợp lệ. Chọn DMM+25, C-HD hoặc Dijkstra.');
    return id;
  }
  function algorithmName(id=S.algorithm) {
    if(id==='dijkstra') return 'Dijkstra (Rust Heap)';
    if(id==='chd') return 'C-HD (Hop-constrained)';
    return 'DMM+25 (BMSSP Rust)';
  }
  function updateControls() {
    const active=['pending','running','paused','starting'].includes(S.status), paused=S.status==='paused';
    const cannotRun = !S.graph || active || S.busy || (S.offline && !S.events.length);
    $('run').disabled = cannotRun;
    $('run-chd').disabled = cannotRun;
    $('run-dijkstra').disabled = cannotRun;
    $('run').hidden = S.offline && S.algorithm !== 'dmm25';
    $('run-chd').hidden = S.offline && S.algorithm !== 'chd';
    $('run-dijkstra').hidden = S.offline && S.algorithm !== 'dijkstra';
    $('run').textContent = S.offline ? '▶ Phát lại DMM+25' : '▶ Chạy DMM+25';
    $('run-chd').textContent = S.offline ? '▶ Phát lại C-HD' : '▶ Chạy C-HD';
    $('run-dijkstra').textContent = S.offline ? '▶ Phát lại Dijkstra' : '▶ Chạy Dijkstra';
    $('active-algorithm').textContent = `Thuật toán ${S.offline ? 'trong bản ghi' : 'hiện tại'}: ${algorithmName()}`;
    $('pivot-legend').hidden = S.algorithm === 'dijkstra';
    $('complete-legend').textContent = S.algorithm === 'dijkstra' ? 'Đã chốt đỉnh' : 'Hoàn tất lô';
    $('pause').disabled = !active || ['pending','starting'].includes(S.status) || (!S.offline && $('mode').value === 'fast');
    $('pause').textContent = paused ? 'Tiếp tục' : 'Tạm dừng';
    $('step').disabled = !paused || (!S.offline && $('mode').value === 'fast');
    $('stop').disabled = !active;
    $('reset').disabled = S.busy;
    for (const id of ['dataset', 'file-input', 'import-button', 'source', 'mode']) $(id).disabled = active || S.busy || (S.offline && id === 'dataset');
    if (S.offline) $('source').disabled = true;
    $('mode').disabled = S.offline || active || S.busy;
    document.querySelectorAll('[data-export]').forEach(b => b.disabled = !S.graph || (['result', 'csv', 'html'].includes(b.dataset.export) && !S.result) || (b.dataset.export === 'trace' && !S.events.length));
    const label = { idle: 'Sẵn sàng', pending: 'Đang xếp hàng…', starting: 'Khởi động…', running: S.offline ? 'Đang phát lại' : 'Đang chạy trực tiếp', paused: S.offline ? 'Đã dừng phát lại' : 'Runner đã tạm dừng', completed: S.offline ? 'Bản ghi ngoại tuyến' : 'Hoàn tất', stopped: 'Đã dừng', error: 'Có lỗi' };
    $('run-status').textContent = label[S.status] || S.status;
    $('run-status').className = 'status ' + (['pending','running', 'starting'].includes(S.status) ? 'running' : S.status === 'paused' ? 'paused' : S.status === 'error' ? 'error' : 'idle');
    $('connection-label').textContent = S.offline ? 'Ngoại tuyến · phát lại' : 'Port 8766 · Cục bộ';
    $('mode-note').textContent = S.offline ? (S.events.length ? 'Phát lại sự kiện đã ghi; không chạy thuật toán mới.' : 'Bản kết quả không kèm trace để phát lại.') : $('mode').value === 'fast' ? 'Runner native không trace và không chờ hoạt họa.' : 'Sự kiện từ runner thật; tạm dừng và từng bước tác động vào tiến trình.';
  }
  function initDistances() { const n=S.graph?.n||0; S.dist=Array(n).fill(null); S.pred=Array(n).fill(null); S.predEdges=Array(n).fill(null); const source=Number($('source').value); if(source>=0&&source<n)S.dist[source]=0; S.activeEdge=null;S.pivots.clear();S.frontier.clear();S.complete.clear();S.level=0;S.maxLevel=0;S.pivotCount=0;S.page=0; }
  function setGraph(graph,keepResult=false) {
    if(!graph||!Number.isInteger(graph.n)||graph.n<1||graph.n>50000||!Array.isArray(graph.edges)||graph.edges.length>500000)throw Error('JSON cần đồ thị gồm n và edges.');
    S.graph=JSON.parse(JSON.stringify(graph));S.comparison={dmm25:null,chd:null,dijkstra:null};const g=S.graph;$('graph-stage').classList.toggle('graph-stage-tall',g.n>=70&&g.n<=150);
    g.edges=g.edges.map((e,i)=>Array.isArray(e)?{id:i,u:e[0],v:e[1],w:e[2]}:{...e,id:e.id??i});
    if(g.edges.some(e=>!Number.isInteger(e.u)||!Number.isInteger(e.v)||e.u<0||e.u>=g.n||e.v<0||e.v>=g.n||!Number.isFinite(e.w)||e.w<0))throw Error('Cạnh không hợp lệ: ID đỉnh phải trong [0,n), trọng số hữu hạn không âm.');
    if(new Set(g.edges.map(e=>e.id)).size!==g.edges.length)throw Error('ID cạnh bị trùng.');
    S.edgeById=new Map(g.edges.map(e=>[e.id,e]));S.adj=Array.from({length:g.n},()=>[]);
    for(const e of g.edges){S.adj[e.u].push(e.v);if(e.v!==e.u)S.adj[e.v].push(e.u);}
    const pairs=new Map();for(const e of g.edges){const key=Math.min(e.u,e.v)+':'+Math.max(e.u,e.v);if(!pairs.has(key))pairs.set(key,[]);pairs.get(key).push(e);}
    for(const list of pairs.values())list.forEach((e,i)=>{e._curve=(i-(list.length-1)/2)*30*(e.u<=e.v?1:-1);e._loopIndex=i;});
    if(!Array.isArray(g.positions)||g.positions.length!==g.n||g.positions.some(p=>!p||!Number.isFinite(p.x)||!Number.isFinite(p.y)))g.positions=layout(g);
    $('source').value=Number.isInteger(g.source)?g.source:0; $('target').value=Number.isInteger(g.target)?g.target:Math.min(g.n-1,5);$('source').max=g.n-1;$('target').max=g.n-1;$('search').max=g.n-1;
    S.selected=Number($('target').value); if(!keepResult){S.result=null;S.events=[];S.recordedTraceInfo=null;S.runId=null;S.status='idle';S.wall=0;}
    $('graph-name').textContent=g.name||'Đồ thị đã nhập';$('stat-n').textContent=integer(g.n);$('stat-m').textContent=integer(g.edges.length);$('graph-empty').hidden=true;
    initDistances();prepareVisible();fit();if(!keepResult)setView('graph');renderEvents();renderAll();updateControls();
  }
  function layout(g) {
    const n=g.n; const points=Array.from({length:n},(_,i)=>{const a=i*2.3999632297;const r=Math.sqrt((i+.5)/n);return {x:500+420*r*Math.cos(a),y:330+270*r*Math.sin(a)};});
    if(n>150)return points;
    for(let t=0;t<130;t++) { const force=Array.from({length:n},()=>({x:0,y:0}));for(let i=0;i<n;i++)for(let j=i+1;j<n;j++){let dx=points[i].x-points[j].x,dy=points[i].y-points[j].y,d2=Math.max(200,dx*dx+dy*dy);const f=1800/d2;force[i].x+=dx*f;force[i].y+=dy*f;force[j].x-=dx*f;force[j].y-=dy*f;}for(const e of g.edges){if(e.u===e.v)continue;const a=points[e.u],b=points[e.v],dx=b.x-a.x,dy=b.y-a.y,d=Math.hypot(dx,dy)||1,f=(d-125)*.018;force[e.u].x+=dx/d*f;force[e.u].y+=dy/d*f;force[e.v].x-=dx/d*f;force[e.v].y-=dy/d*f;}for(let i=0;i<n;i++){const p=points[i];p.x+=Math.max(-8,Math.min(8,force[i].x+(500-p.x)*.001));p.y+=Math.max(-8,Math.min(8,force[i].y+(330-p.y)*.001));}}
    return points;
  }
  function prepareVisible() {
    if(!S.graph)return; const n=S.graph.n;if(n<=150)S.visible=Array.from({length:n},(_,i)=>i);else{
      const chosen=new Set();const add=v=>{if(Number.isInteger(v)&&v>=0&&v<n&&chosen.size<200)chosen.add(v);};add(S.selected);add(Number($('source').value));
      const path=getPath();path.nodes.slice(0,120).forEach(add);const queue=[...chosen];let q=0,scans=0;
      while(q<queue.length&&chosen.size<200&&scans<50000){for(const v of S.adj[queue[q++]]){scans++;if(!chosen.has(v)){add(v);queue.push(v);}if(chosen.size>=200||scans>=50000)break;}}
      for(let i=0;chosen.size<Math.min(200,n);i++)add(i);S.visible=[...chosen];
    }
    const keep=new Set(S.visible);S.edges=S.graph.edges.filter(e=>keep.has(e.u)&&keep.has(e.v));S.edgeTotal=S.edges.length;S.edges=S.edges.slice(0,1800);
    $('render-note').textContent=n>150?`Vùng chọn ${S.visible.length}/${integer(n)} đỉnh · Canvas · tìm ID để chuyển vùng`:'Kéo đỉnh để sắp xếp · cuộn để thu phóng';
    $('stage-note').textContent=n>150?`HIỂN THỊ MỘT PHẦN · ${S.visible.length} đỉnh · ${integer(S.edges.length)}/${integer(S.graph.edges.length)} cạnh${S.edgeTotal>1800?' · giới hạn 1.800 cạnh':''}`:'Mũi tên thể hiện chiều cạnh · chọn đỉnh để xem đường đi';
  }
  function getPath() {
    const source=Number(S.result?.source??$('source').value),target=Number($('target').value),dist=S.result?.distances||S.dist,pred=S.result?.predecessors||S.pred,pEdges=S.result?.predecessor_edges||S.predEdges;
    if(!S.graph||!Number.isInteger(target)||target<0||target>=S.graph.n)return {nodes:[],edges:new Set(),error:'Đỉnh đích ngoài phạm vi.'};
    if(dist[target]==null)return {nodes:[],edges:new Set(),error:S.result?'Không có đường đi từ nguồn đến đỉnh này.':'Chưa tìm thấy đường đến đỉnh này.'};
    const nodes=[],edges=new Set(),seen=new Set();let v=target;
    while(v!==source){if(seen.has(v)||nodes.length>S.graph.n)return {nodes:[],edges:new Set(),error:'Chuỗi đỉnh trước có chu trình; không hiển thị đường sai.'};seen.add(v);nodes.push(v);const u=pred[v];if(!Number.isInteger(u)||u<0||u>=S.graph.n)return {nodes:[],edges:new Set(),error:'Chuỗi đỉnh trước chưa đầy đủ.'};let edge=S.edgeById.get(pEdges[v]);if(!edge||edge.u!==u||edge.v!==v){edge=S.graph.edges.find(e=>e.u===u&&e.v===v&&Math.abs((dist[u]??Infinity)+e.w-dist[v])<=1e-7*Math.max(1,Math.abs(dist[v])));}if(!edge)return {nodes:[],edges:new Set(),error:'Không tìm thấy cạnh hợp lệ của đường đi.'};edges.add(edge.id);v=u;}
    nodes.push(source);nodes.reverse();return {nodes,edges,cost:dist[target]};
  }
  function pathSummary() {const p=getPath();$('path-text').textContent=p.error||((S.result?'':'Tạm thời: ')+(p.nodes.length>50?p.nodes.slice(0,24).join(' → ')+' → … → '+p.nodes.slice(-10).join(' → '):p.nodes.join(' → ')));$('path-cost').textContent=p.error?'—':fmt(p.cost);return p;}
  function fit() {if(!S.graph||!S.visible.length)return;const points=S.visible.map(v=>S.graph.positions[v]);const xs=points.map(p=>p.x),ys=points.map(p=>p.y),minX=Math.min(...xs)-55,maxX=Math.max(...xs)+55,minY=Math.min(...ys)-55,maxY=Math.max(...ys)+55;const rect=$('graph-stage').getBoundingClientRect();const scale=Math.min(rect.width/(maxX-minX),rect.height/(maxY-minY),2.2);S.viewport={scale,x:rect.width/2-(minX+maxX)/2*scale,y:rect.height/2-(minY+maxY)/2*scale};}
  function scheduleRender() {if(S.renderPending)return;S.renderPending=true;requestAnimationFrame(()=>{S.renderPending=false;renderAll();});}
  function nodeStyle(v,path) {const source=Number(S.result?.source??$('source').value);if(v===source)return {fill:colors.source,stroke:colors.source,text:'#fff'};if(S.pivots.has(v))return {fill:colors.pivot,stroke:'#9e81bd',text:'#654d83'};if(S.complete.has(v))return {fill:colors.complete,stroke:'#84b294',text:'#335e40'};if(S.frontier.has(v))return {fill:colors.frontier,stroke:'#82b2c7',text:'#3a6e85'};if(S.result&&S.result.distances[v]!=null)return {fill:'#e6f1e8',stroke:'#a6c4ad',text:'#486e52'};return {fill:'#fff',stroke:path.nodes.includes(v)?colors.path:'#b7cdc0',text:'#536d5c'};}
  function edgeStyle(e,path) {const active=S.activeEdge===e.id,inPath=path.edges.has(e.id),isTree=(S.result?.predecessor_edges||S.predEdges)[e.v]===e.id;return {color:active?'#3c91bb':inPath?colors.path:isTree&&S.view==='tree'?'#69a080':'#c8d7ce',width:active?3.7:inPath?3:isTree&&S.view==='tree'?1.8:1.2,opacity:active?1:S.view==='path'&&!inPath?.16:S.view==='tree'&&!isTree?.2:.9};}
  function geometry(e) {
    const a=S.graph.positions[e.u],b=S.graph.positions[e.v],dx=b.x-a.x,dy=b.y-a.y,d=Math.hypot(dx,dy)||1,curve=e._curve||0;
    if(e.u===e.v){const spread=43+(e._loopIndex||0)*18,top=55+(e._loopIndex||0)*18,start={x:a.x-11,y:a.y-12},end={x:a.x+11,y:a.y-12},c1={x:a.x-spread,y:a.y-top},c2={x:a.x+spread,y:a.y-top};return {loop:true,a:start,b:end,c1,c2,d:`M ${start.x} ${start.y} C ${c1.x} ${c1.y}, ${c2.x} ${c2.y}, ${end.x} ${end.y}`,mx:a.x,my:a.y-top*.78};}
    const cx=(a.x+b.x)/2-dy/d*curve,cy=(a.y+b.y)/2+dx/d*curve,da=Math.hypot(cx-a.x,cy-a.y)||1,db=Math.hypot(b.x-cx,b.y-cy)||1;
    const start={x:a.x+(cx-a.x)/da*17,y:a.y+(cy-a.y)/da*17},end={x:b.x-(b.x-cx)/db*20,y:b.y-(b.y-cy)/db*20};
    return {a:start,b:end,c:{x:cx,y:cy},d:`M ${start.x} ${start.y} Q ${cx} ${cy} ${end.x} ${end.y}`,mx:(start.x+2*cx+end.x)/4,my:(start.y+2*cy+end.y)/4};
  }
  function makeSVG(path) {
    const rect=$('graph-stage').getBoundingClientRect(),svg=el('svg',{xmlns:NS,viewBox:`0 0 ${rect.width} ${rect.height}`,width:rect.width,height:rect.height,role:'img','aria-label':`Đồ thị có hướng ${algorithmName()}`});const defs=el('defs');for(const [id,color]of [['arrow','#bdcfc2'],['path-arrow',colors.path],['tree-arrow','#69a080'],['active-arrow','#3c91bb']]){const marker=el('marker',{id,'markerWidth':7,'markerHeight':7,refX:6,refY:3.5,orient:'auto',markerUnits:'userSpaceOnUse'});marker.append(el('path',{d:'M 0 0 L 7 3.5 L 0 7 Z',fill:color}));defs.append(marker);}svg.append(defs);const group=el('g',{transform:`translate(${S.viewport.x} ${S.viewport.y}) scale(${S.viewport.scale})`});svg.append(group);
    for(const e of S.edges){const g=geometry(e),style=edgeStyle(e,path),edge=el('path',{d:g.d,fill:'none',stroke:style.color,'stroke-width':style.width,opacity:style.opacity,'marker-end':`url(#${S.activeEdge===e.id?'active-arrow':path.edges.has(e.id)?'path-arrow':S.view==='tree'&&(S.result?.predecessor_edges||S.predEdges)[e.v]===e.id?'tree-arrow':'arrow'})`});edge.append(el('title',{},`Cạnh #${e.id}: ${e.u} → ${e.v}, trọng số ${fmt(e.w)}`));group.append(edge);if($('weights').checked&&(S.graph.n<=150||S.edges.length<350)&&style.opacity>.3)group.append(el('text',{x:g.mx,y:g.my-5,class:'edge-weight','text-anchor':'middle','font-size':12,fill:'#819488','paint-order':'stroke',stroke:'#fcfdfb','stroke-width':3},fmt(e.w)));}
    for(const v of S.visible){const p=S.graph.positions[v],style=nodeStyle(v,path),node=el('g',{class:'node','data-node':v,transform:`translate(${p.x} ${p.y})`,tabindex:0,role:'button','aria-label':`Đỉnh ${v}, khoảng cách ${fmt(S.dist[v])}`});if(v===S.selected)node.append(el('circle',{r:22,fill:'none',stroke:'#e8bd71','stroke-width':2,opacity:.6}));node.append(el('circle',{r:16,fill:style.fill,stroke:style.stroke,'stroke-width':2}));node.append(el('text',{class:'node-label',fill:style.text,style:`fill:${style.text};font-size:${v>=1000?10:14}px`,'text-anchor':'middle','dominant-baseline':'central','font-size':v>=1000?10:14,'font-family':'Segoe UI,Arial,sans-serif'},v));node.append(el('title',{},`Đỉnh ${v} · d = ${fmt(S.dist[v])}`));group.append(node);}return svg;
  }
  function drawCanvas(path) {const canvas=$('graph-canvas'),rect=$('graph-stage').getBoundingClientRect(),dpr=window.devicePixelRatio||1;canvas.width=Math.round(rect.width*dpr);canvas.height=Math.round(rect.height*dpr);const c=canvas.getContext('2d');c.scale(dpr,dpr);c.translate(S.viewport.x,S.viewport.y);c.scale(S.viewport.scale,S.viewport.scale);for(const e of S.edges){const g=geometry(e),style=edgeStyle(e,path);c.strokeStyle=style.color;c.globalAlpha=style.opacity;c.lineWidth=style.width;c.beginPath();c.moveTo(g.a.x,g.a.y);if(g.loop)c.bezierCurveTo(g.c1.x,g.c1.y,g.c2.x,g.c2.y,g.b.x,g.b.y);else c.quadraticCurveTo(g.c.x,g.c.y,g.b.x,g.b.y);c.stroke();{const tangent=g.loop?g.c2:g.c;const angle=Math.atan2(g.b.y-tangent.y,g.b.x-tangent.x);c.fillStyle=style.color;c.beginPath();c.moveTo(g.b.x,g.b.y);c.lineTo(g.b.x-8*Math.cos(angle-.4),g.b.y-8*Math.sin(angle-.4));c.lineTo(g.b.x-8*Math.cos(angle+.4),g.b.y-8*Math.sin(angle+.4));c.closePath();c.fill();}if($('weights').checked&&S.edges.length<350&&style.opacity>.3){c.font='12px Segoe UI';c.textAlign='center';c.fillStyle='#7f9487';c.fillText(fmt(e.w),g.mx,g.my-6);}}c.globalAlpha=1;for(const v of S.visible){const p=S.graph.positions[v],style=nodeStyle(v,path);if(v===S.selected){c.beginPath();c.arc(p.x,p.y,22,0,Math.PI*2);c.strokeStyle='#e8bd71';c.lineWidth=2;c.stroke();}c.beginPath();c.arc(p.x,p.y,16,0,Math.PI*2);c.fillStyle=style.fill;c.fill();c.strokeStyle=style.stroke;c.lineWidth=2;c.stroke();c.fillStyle=style.text;c.font=`600 ${v>=1000?8:10}px Segoe UI`;c.textAlign='center';c.textBaseline='middle';c.fillText(String(v),p.x,p.y);}}
  function renderGraph(path) {
    if(!S.graph)return;
    const large=S.graph.n>150,svg=$('graph-svg'),canvas=$('graph-canvas');
    // SVGElement.hidden is not a reflected HTMLElement property. Set the
    // attribute itself so CSS actually removes the old renderer from layout.
    svg.toggleAttribute('hidden',large);
    svg.setAttribute('aria-hidden',String(large));
    canvas.toggleAttribute('hidden',!large);
    canvas.setAttribute('aria-hidden',String(!large));
    if(large){svg.replaceChildren();drawCanvas(path);}
    else{const image=makeSVG(path);svg.setAttribute('viewBox',image.getAttribute('viewBox'));svg.replaceChildren(...image.childNodes);}
    const v=S.selected;
    if(v!==null){const card=$('selection-card');card.replaceChildren();const title=document.createElement('strong');title.textContent=`Đỉnh ${v}${v===Number($('source').value)?' · nguồn':''}`;card.append(title,document.createTextNode(`Khoảng cách: ${fmt((S.result?.distances||S.dist)[v])}`));card.hidden=false;}else $('selection-card').hidden=true;
  }
  function renderTable() {if(!S.graph)return;const body=$('distance-body');body.replaceChildren();const dist=S.result?.distances||S.dist,pred=S.result?.predecessors||S.pred,pEdges=S.result?.predecessor_edges||S.predEdges,start=S.page*20,end=Math.min(S.graph.n,start+20);for(let v=start;v<end;v++){const tr=document.createElement('tr');tr.tabIndex=0;tr.className=v===S.selected?'selected':'';const values=[v,fmt(dist[v]),pred[v]??'—',pEdges[v]??'—',v===Number(S.result?.source??$('source').value)?'Nguồn':S.result?(dist[v]==null?'Không tới được':'Tới được'):S.complete.has(v)?(S.algorithm==='dijkstra'?'Đã chốt':'Hoàn tất lô'):S.pivots.has(v)?'Pivot':S.frontier.has(v)?'Đã relax':'Chưa thăm'];for(const [i,value]of values.entries()){const td=document.createElement('td');td.textContent=String(value);if(i===4)td.className='cell-status';tr.append(td);}tr.addEventListener('click',()=>selectVertex(v,false));tr.addEventListener('keydown',e=>{if(e.key==='Enter')selectVertex(v,false);});body.append(tr);}$('table-count').textContent=`${start+1}–${end} / ${integer(S.graph.n)} đỉnh`;$('page-number').textContent=String(S.page+1);$('prev-page').disabled=S.page===0;$('next-page').disabled=end>=S.graph.n;}
  function renderMetrics() {
    const result=S.result,isDijkstra=S.algorithm==='dijkstra',compute=result?.compute_ms??result?.native_compute_ms;
    $('wall-time').textContent=fmt(result?.wall_ms??S.wall)+' ms';
    const fast=result?.mode==='fast'||result?.mode==='batch'||(!S.offline&&$('mode').value==='fast');
    $('compute-time').textContent=compute!=null?fmt(compute)+' ms':'—';
    $('compute-label').textContent=`${algorithmName()} · ${fast?'không trace':'có trace'}`;
    $('compute-note').textContent=fast?'Đo không trace · đơn lẻ':'Không phải benchmark thuần';
    $('reachable').textContent=result?`${integer(result.distances.filter(d=>d!==null).length)} / ${integer(S.graph.n)}`:'—';
    $('reachable-note').textContent=result?'Trên toàn bộ đồ thị':'Chờ kết quả được kiểm chứng';
    $('verification').textContent=result?(result.verified?'Khớp Oracle':'Lỗi đối chiếu'):'Chưa chạy';
    $('verification').className='verification'+(result?(result.verified?' pass':' fail'):'');
    $('verification-note').textContent=result?(result.verified?'Khoảng cách khớp 100% độc lập':'Xem chi tiết trong JSON'):'So toàn bộ khoảng cách';

    // Parameters panel update
    const stats=result?.stats||{};
    $('stat-nv').textContent=stats.n_transformed!=null?integer(stats.n_transformed):'—';
    $('stat-mv').textContent=stats.m_transformed!=null?integer(stats.m_transformed):'—';
    $('stat-k').textContent=stats.k!=null?integer(stats.k):'—';
    $('stat-t').textContent=stats.t!=null?integer(stats.t):'—';
    $('stat-levels').textContent=stats.levels!=null?integer(stats.levels):(isDijkstra?'0':'—');
    $('stat-relax').textContent=stats.relax_calls!=null?integer(stats.relax_calls):'—';
  }
  function recordComparison(result) {
    const id=algorithmId(result),old=S.comparison[id],compute=result.compute_ms??result.native_compute_ms;
    const stats=result.stats||{};
    const latest={
      algorithm_id:id,source:result.source,mode:result.mode,verified:result.verified,
      compute_ms:compute??null,control_wait_ms:result.control_wait_ms??null,
      n_transformed:stats.n_transformed,m_transformed:stats.m_transformed,
      k:stats.k,t:stats.t,levels:stats.levels,relax_calls:stats.relax_calls
    };
    let noTrace=old?.noTrace||null;
    if(result.mode==='fast'||result.mode==='batch'){
      noTrace={ms:compute,label:algorithmName(id)+' · không trace',verified:result.verified};
    }
    S.comparison[id]={latest,noTrace};
  }
  function restoreComparison(comparison) {
    if(!comparison) return;
    for(const id of ['dmm25','chd','dijkstra']){
      const item=comparison[id];
      if(item?.latest?.source===Number($('source').value))S.comparison[id]=item;
    }
  }
  function renderComparison() {
    const body=$('comparison-body');body.replaceChildren();
    $('comparison-context').textContent=S.graph?`${integer(S.graph.n)} đỉnh · ${integer(S.graph.edges.length)} cạnh · nguồn ${$('source').value}`:'Chưa có đồ thị';
    const algos=[
      {id:'dmm25',name:'DMM+25',sub:'arXiv:2504.17033 · gadget'},
      {id:'chd',name:'C-HD',sub:'vals.ai · hop-constrained'},
      {id:'dijkstra',name:'Dijkstra',sub:'Rust Binary Heap'},
    ];
    for(const a of algos){
      const item=S.comparison[a.id],latest=item?.latest,tr=document.createElement('tr');
      if(a.id===S.algorithm&&latest)tr.className='comparison-current';
      const values=[
        a.name,
        latest?(latest.verified?'Khớp Oracle':'Lỗi'):'Chưa chạy',
        latest?.compute_ms!=null?`${fmt(latest.compute_ms)} ms`:'—',
        latest?.n_transformed!=null?`${integer(latest.n_transformed)} / ${integer(latest.m_transformed)}`:'—',
        latest?.k!=null?`k=${latest.k}, t=${latest.t}, L=${latest.levels}`:(a.id==='dijkstra'&&latest?'O(m log n)':'—'),
        latest?.control_wait_ms!=null?`${fmt(latest.control_wait_ms)} ms`:'—'
      ];
      values.forEach((value,index)=>{
        const td=document.createElement('td');td.textContent=value;
        if(index===0){const small=document.createElement('small');small.textContent=a.sub;td.append(small);}
        if(index===1&&latest)td.className=latest.verified?'comparison-pass':'comparison-fail';
        if(index===2&&latest){const small=document.createElement('small');small.textContent=latest.mode==='fast'||latest.mode==='batch'?'Không trace':'Có trace';td.append(small);}
        tr.append(td);
      });
      body.append(tr);
    }
  }
  function renderAll() {if(!S.graph){renderComparison();updateControls();return;}const p=pathSummary();renderGraph(p);renderTable();renderMetrics();renderComparison();$('trace-counters').textContent=S.algorithm==='dijkstra'?`Dijkstra · đã chốt ${integer(S.complete.size)} đỉnh`:`Mức đệ quy ${S.level} / ${S.maxLevel} · Pivot ${integer(S.pivotCount)}`;}
  const eventNames={start:'Bắt đầu',enter:'BMSSP vào',exit:'BMSSP ra',pivots:'FindPivots',relax:'Relax',complete:'Hoàn tất lô',result:'Kết quả',error:'Lỗi',stopped:'Đã dừng',status:'Điều khiển'};
  function eventDetail(e) {switch(e.type){case 'relax':return `${e.u} → ${e.v} · cạnh #${e.edge_id??'?'} · d = ${fmt(e.distance)}${S.algorithm!=='dijkstra'?` · mức ${e.level??0}`:''}`;case 'enter':return `Mức ${e.level??0} · B = ${fmt(e.bound)} · |S| = ${e.sources?.length??e.source_count??'?'}`;case 'exit':return `Mức ${e.level??0} · B′ = ${fmt(e.bound)} · ${e.vertices?.length??e.count??'?'} đỉnh`;case 'pivots':return `Mức ${e.level??0} · ${(e.vertices||[]).slice(0,16).join(', ')}${e.vertices?.length>16?' …':''}`;case 'complete':return S.algorithm==='dijkstra'?`Chốt đỉnh ${(e.vertices||[]).join(', ')} theo khoảng cách nhỏ nhất`:`Mức ${e.level??0} · ${e.vertices?.length??0} đỉnh hoàn tất theo lô`;case 'start':return `${e.algorithm||algorithmName(algorithmId(e))} · nguồn ${e.source??$('source').value} · ${integer(e.n||S.graph.n)} đỉnh`;case 'result':return e.result?.verified?'Đã đối chiếu toàn bộ khoảng cách và đường đi.':'Đã nhận kết quả cuối.';case 'error':return e.error||e.message||'Runner báo lỗi.';case 'status':return e.status||'';case 'stopped':return 'Phiên đã dừng. Chưa có kết quả cuối.';default:return 'Sự kiện từ runner';}}
  let eventRenderPending=false;
  function scheduleEvents() {if(eventRenderPending)return;eventRenderPending=true;requestAnimationFrame(()=>{eventRenderPending=false;renderEvents();});}
  function renderEvents() {const list=$('events-list'),visible=S.offline&&S.replay?S.events.slice(0,S.cursor):S.events;list.replaceChildren();const tail=visible.slice(-150);for(const e of tail){const li=document.createElement('li');li.className='event-row';for(const [className,value]of [['event-seq','#'+(e.seq??'?')],['event-type',S.algorithm==='dijkstra'&&e.type==='complete'?'Chốt đỉnh':eventNames[e.type]||e.type],['event-detail',eventDetail(e)]]){const span=document.createElement('span');span.className=className;span.textContent=value;li.append(span);}list.append(li);}if(!tail.length){const li=document.createElement('li');li.className='empty-table';li.textContent='Chưa có sự kiện. Chẽn chế độ trực tiếp và chạy một thuật toán.';list.append(li);}$('event-count').textContent=integer(visible.length);list.scrollTop=list.scrollHeight;const sampled=S.result?.trace?.sampled||S.recordedTraceInfo?.sampled||S.graph?.metadata?.trace_sampled;$('trace-note').textContent=`${S.offline?'PHÁT LẠI BẢN GHI. ':''}${sampled?'Trace đã lấy mẫu; không bao gồm mọi sự kiện. ':''}Hiển thị tối đa 150 sự kiện gần nhất; xuất JSONL để xem toàn bộ bản ghi${S.offline?` · ${S.replay?S.cursor:S.events.length}/${S.events.length}`:''}.`;}
  function applyEvent(e,replay=false) {if(!e||typeof e!=='object')return;if(e.type!=='status')S.activeEdge=null;if(e.type==='relax'){const edge=S.edgeById.get(e.edge_id);if(edge&&edge.u===e.u&&edge.v===e.v)S.activeEdge=edge.id;else{const candidate=S.graph.edges.find(edge=>edge.u===e.u&&edge.v===e.v&&Math.abs((S.dist[e.u]??Infinity)+edge.w-e.distance)<=1e-9*Math.max(1,Math.abs(e.distance)));if(candidate)S.activeEdge=candidate.id;}}if(e.level!==undefined){S.level=e.level;S.maxLevel=Math.max(S.maxLevel,Number(e.level)||0);}switch(e.type){case 'start':if(e.algorithm_id||e.algorithm)S.algorithm=algorithmId(e);if(!replay)S.status='running';S.graph.metadata={...S.graph.metadata,trace_sampled:Boolean(e.sampled)};if(Number.isInteger(e.source)){$('source').value=e.source;S.dist[e.source]=0;}break;case 'pivots':S.pivots=new Set((e.vertices||[]).filter(Number.isInteger));S.pivotCount+=S.pivots.size;break;case 'relax':if(Number.isInteger(e.v)&&e.v>=0&&e.v<S.dist.length){S.dist[e.v]=e.distance;S.pred[e.v]=e.predecessor??e.u;S.predEdges[e.v]=e.edge_id??null;S.frontier.add(e.v);}break;case 'complete':for(const [i,v]of(e.vertices||[]).entries()){if(v>=0&&v<S.dist.length){S.complete.add(v);S.frontier.delete(v);if(e.distances&&e.distances[i]!==undefined)S.dist[v]=e.distances[i];}}break;case 'result':if(e.result)setResult(e.result);S.status='completed';closeStream();if(replay)clearTimeout(S.timer);break;case 'error':S.status='error';notice(e.error||e.message||'Runner gặp lỗi.',true);closeStream();break;case 'stopped':S.status='stopped';closeStream();break;case 'status':if(!replay&&['running','paused','completed','stopped','error'].includes(e.status))S.status=e.status;break;}scheduleRender();updateControls();}
  function setResult(result) {if(!result||!Array.isArray(result.distances)||result.distances.length!==S.graph.n)throw Error('Kết quả không khớp số đỉnh của đồ thị.');const mode=result.mode||result.provenance?.execution_mode||$('mode').value;S.result={...result,algorithm_id:algorithmId(result),mode};S.algorithm=S.result.algorithm_id;if(Number.isInteger(result.source))$('source').value=result.source;recordComparison(S.result);S.dist=result.distances.slice();S.pred=(result.predecessors||Array(S.graph.n).fill(null)).slice();S.predEdges=(result.predecessor_edges||Array(S.graph.n).fill(null)).slice();S.wall=result.wall_ms??S.wall;S.status='completed';if(S.graph.n>150){prepareVisible();fit();}}
  function closeStream() {if(S.es)S.es.close();S.es=null;}
  async function run(algorithm='dmm25') {
    if(S.offline){startReplay();return;}
    S.algorithm=requestedAlgorithm(algorithm);
    const source=Number($('source').value), mode=$('mode').value;
    if(!Number.isInteger(source)||source<0||source>=S.graph.n)throw Error(`Nguồn phải là số nguyên từ 0 đến ${S.graph.n-1}.`);
    S.busy=true;S.status='starting';updateControls();closeStream();S.result=null;S.events=[];S.wall=0;initDistances();renderEvents();renderAll();
    try {
      const stored=await api('/api/graphs',{graph:exportGraph()});
      S.graph.id=stored.id;
      const response=await api('/api/runs',{graph_id:stored.id,source,algorithm:S.algorithm,mode,delay:Number($('speed').value)});
      S.runId=response.run_id;S.status=response.status||'running';S.startTime=performance.now();S.busy=false;updateControls();
      const id=S.runId,es=new EventSource(`/api/runs/${encodeURIComponent(id)}/events`);S.es=es;
      es.onmessage=ev=>{if(S.runId!==id)return;try{const event=JSON.parse(ev.data);if(event.run_id&&event.run_id!==id)return;const last=S.events[S.events.length-1];if(last&&Number.isFinite(event.seq)&&event.seq<=last.seq)return;S.events.push(event);applyEvent(event);scheduleEvents();}catch(error){notice('Không đọc được sự kiện: '+error.message,true);}};
      es.onerror=async()=>{if(S.es!==es)return;es.close();S.es=null;try{const snapshot=await api(`/api/runs/${encodeURIComponent(id)}`);if(snapshot.result){setResult(snapshot.result);renderAll();updateControls();}else{S.status=snapshot.status||'error';notice(snapshot.error||'Luồng sự kiện đã ngắt. Có thể đặt lại và chạy lại.',true);updateControls();}}catch(error){S.status='error';notice(error.message,true);updateControls();}};
    } catch(error) {
      S.busy=false;
      if(S.status==='starting')S.status='error';
      updateControls();
      throw error;
    }
  }
  async function control(action) {
    if(S.offline){if(action==='pause'){clearTimeout(S.timer);S.status='paused';}if(action==='resume'){S.status='running';replayTick();}if(action==='step'){replayOne();if(S.cursor<S.events.length)S.status='paused';}if(action==='stop'){clearTimeout(S.timer);S.status='stopped';}updateControls();return;}
    if(!S.runId)return;const response=await api(`/api/runs/${encodeURIComponent(S.runId)}/control`,{action,delay:Number($('speed').value)});S.status=response.status||S.status;updateControls();
  }
  function replayOne() {if(S.cursor>=S.events.length){S.status='completed';updateControls();return;}const e=S.events[S.cursor++];applyEvent(e,true);renderEvents();renderAll();if(S.cursor>=S.events.length){S.status='completed';updateControls();}}
  function replayTick() {if(S.status!=='running')return;replayOne();if(S.status==='running')S.timer=setTimeout(replayTick,Math.max(16,Number($('speed').value)));}
  function startReplay() {clearTimeout(S.timer);S.replay=true;S.cursor=0;S.result=null;initDistances();S.status='running';renderEvents();replayTick();updateControls();}
  async function reset() {if(['pending','starting','running','paused'].includes(S.status)&&!S.offline)await control('stop');closeStream();clearTimeout(S.timer);S.runId=null;S.result=null;S.status='idle';S.wall=0;if(!S.offline){S.events=[];S.cursor=0;}else{S.replay=true;S.cursor=0;}initDistances();renderEvents();renderAll();updateControls();}
  function selectVertex(v,center=true) {if(!S.graph||!Number.isInteger(v)||v<0||v>=S.graph.n){notice('ID đỉnh ngoài phạm vi đồ thị.',true);return;}S.selected=v;$('target').value=v;S.page=Math.floor(v/20);prepareVisible();if(center){if(S.graph.n>150)fit();else{const rect=$('graph-stage').getBoundingClientRect(),p=S.graph.positions[v];S.viewport.x=rect.width/2-p.x*S.viewport.scale;S.viewport.y=rect.height/2-p.y*S.viewport.scale;}}renderAll();}
  function worldPoint(event) {const rect=$('graph-stage').getBoundingClientRect();return {x:(event.clientX-rect.left-S.viewport.x)/S.viewport.scale,y:(event.clientY-rect.top-S.viewport.y)/S.viewport.scale};}
  function zoom(factor,x,y) {const rect=$('graph-stage').getBoundingClientRect();x??=rect.width/2;y??=rect.height/2;const old=S.viewport.scale,next=Math.max(.08,Math.min(8,old*factor));S.viewport.x=x-(x-S.viewport.x)*next/old;S.viewport.y=y-(y-S.viewport.y)*next/old;S.viewport.scale=next;renderGraph(getPath());}
  function exportGraph() {const graph=JSON.parse(JSON.stringify(S.graph));for(const e of graph.edges){delete e._curve;delete e._loopIndex;}graph.source=Number($('source').value);graph.target=Number($('target').value);graph.metadata={...graph.metadata,view:S.view,viewport:S.viewport};return graph;}
  function download(text,name,type='application/json') {const url=URL.createObjectURL(new Blob([text],{type}));const a=document.createElement('a');a.href=url;a.download=name;document.body.append(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),2000);}
  async function exportHTML() {if(!S.assets&&!S.offline){const responses=await Promise.all([fetch('style.css'),fetch('app.js')]);if(responses.some(r=>!r.ok))throw Error('Không tải được tài nguyên để tạo HTML ngoại tuyến.');S.assets={css:await responses[0].text(),js:await responses[1].text()};}const clone=document.documentElement.cloneNode(true);clone.querySelectorAll('script,style,link[rel="stylesheet"]').forEach(n=>n.remove());const style=document.createElement('style');style.textContent=S.assets?.css||document.querySelector('style')?.textContent||'';clone.querySelector('head').append(style);const data=document.createElement('script');data.id='offline-data';data.type='application/json';data.textContent=JSON.stringify({graph:exportGraph(),result:S.result,events:S.events,view:S.view,comparison:S.comparison}).replace(/</g,'\\u003c');clone.querySelector('body').append(data);const script=document.createElement('script');script.textContent=(S.assets?.js||document.querySelector('script[data-app]')?.textContent||'').replace(/<\/script/gi,'<\\/script');script.dataset.app='true';clone.querySelector('body').append(script);download('<!DOCTYPE html>\n'+clone.outerHTML,`${S.algorithm}-demo.html`,'text/html;charset=utf-8');}
  async function exportFile(kind) {if(!S.graph)return;if(kind==='graph'){download(JSON.stringify(exportGraph(),null,2),'graph.json');return;}if(kind==='svg'){exportSVG();return;}if(kind==='html'){await exportHTML();return;}if(!S.offline&&S.runId){const a=document.createElement('a');a.href=`/api/runs/${encodeURIComponent(S.runId)}/export?kind=${kind}`;a.download='';a.click();return;}if(kind==='result')download(JSON.stringify({graph:exportGraph(),result:S.result,events:S.events},null,2),'result.json');if(kind==='trace')download(S.events.map(e=>JSON.stringify(e)).join('\n')+'\n','trace.jsonl','application/x-ndjson');if(kind==='csv'){const rows=['vertex,distance,predecessor,predecessor_edge,reachable'];S.result.distances.forEach((d,v)=>rows.push([v,d??'',S.result.predecessors?.[v]??'',S.result.predecessor_edges?.[v]??'',d!==null].join(',')));download('\uFEFF'+rows.join('\r\n'),'distances.csv','text/csv;charset=utf-8');}}
  function exportSVG() {
    const svg=makeSVG(getPath()),rect=$('graph-stage').getBoundingClientRect(),width=rect.width,height=rect.height;
    svg.setAttribute('viewBox',`0 0 ${width} ${height+78}`);svg.setAttribute('height',height+78);
    svg.insertBefore(el('rect',{width:'100%',height:'100%',fill:'#fcfdfb'}),svg.firstChild);
    svg.append(el('rect',{x:0,y:height,width,height:78,fill:'#fff'}),el('line',{x1:0,y1:height,x2:width,y2:height,stroke:'#dce5df'}));
    const text=(x,y,value,size=10,color='#647d6b')=>svg.append(el('text',{x,y,'font-size':size,fill:color,'font-family':'Segoe UI,Arial,sans-serif'},value));
    const viewName={graph:'Đồ thị đầu vào',tree:'Cây đường đi ngắn nhất',path:'Đường đến đích'}[S.view];
    text(16,height+23,`${algorithmName()} · ${viewName} · nguồn ${$('source').value} · đích ${$('target').value}`,11,'#234e3c');
    text(16,height+41,`${integer(S.graph.n)} đỉnh · ${integer(S.graph.edges.length)} cạnh${S.graph.n>150?` · vùng chọn ${S.visible.length} đỉnh (một phần)`:''}`);
    const legend=[['#127366','Nguồn'],['#3c91bb','Relax'],['#9e81bd',S.algorithm==='dijkstra'?'':'Pivot'],['#84b294',S.algorithm==='dijkstra'?'Đã chốt':'Hoàn tất'],['#e29b37','Đường chọn']];
    legend.filter(([,label])=>label).forEach(([color,label],i)=>{const x=16+i*Math.min(90,(width-35)/5);svg.append(el('circle',{cx:x+4,cy:height+60,r:3.5,fill:color}));text(x+13,height+63,label,9);});
    download(new XMLSerializer().serializeToString(svg),S.view==='graph'?'input.svg':'output.svg','image/svg+xml');
  }
  function loadRecording(payload) {const graph=payload.graph||S.graph;if(!graph)throw Error('Kết quả chưa kèm đồ thị; hãy nhập graph.json trước.');S.offline=true;S.imported=true;closeStream();clearTimeout(S.timer);setGraph(graph);if(payload.result)setResult(payload.result);else if(payload.algorithm||payload.algorithm_id)S.algorithm=algorithmId(payload);if(payload.comparison)restoreComparison(payload.comparison);S.recordedTraceInfo=payload.result?.trace||null;S.events=Array.isArray(payload.events)?payload.events:[];S.cursor=S.events.length;S.replay=false;S.view=payload.view||graph.metadata?.view||'graph';setView(S.view);$('dataset').replaceChildren(new Option(graph.name||'Bản ghi đã nhập','offline'));renderEvents();renderAll();updateControls();}
  async function importFile(file) {if(!file)return;const text=await file.text();let payload;try{payload=JSON.parse(text);}catch{if(/\.(jsonl|ndjson)$/i.test(file.name)){const events=text.split(/\r?\n/).filter(s=>s.trim()).map(s=>JSON.parse(s));const result=[...events].reverse().find(e=>e.type==='result')?.result;loadRecording({graph:S.graph,result,events});notice('Đã nhập trace; đây là phát lại bản ghi.');return;}}
    if(payload&&(payload.result||payload.distances||payload.events)){loadRecording(payload.distances?{result:payload}:payload);notice('Đã mở bản ghi. Phát lại không chạy thuật toán mới.');return;}
    if(S.offline){if(!payload)throw Error('Chế độ ngoại tuyến chỉ nhập graph.json; TXT cần mở ứng dụng cục bộ.');S.offline=true;setGraph(payload);$('dataset').replaceChildren(new Option(payload.name||file.name,'offline'));notice('Đã nhập đồ thị ngoại tuyến. Cần ứng dụng cục bộ để chạy mới.');updateControls();return;}
    const response=await api('/api/graphs',payload?{graph:payload}:{text,format:'txt',name:file.name});setGraph(response.graph);$('dataset').add(new Option(response.graph.name||file.name,response.id));$('dataset').value=response.id;$('dataset-description').textContent='Dữ liệu do bạn nhập; ID đỉnh bắt đầu từ 0.';notice('Đã nhập đồ thị. Chọn nguồn và chạy BMSSP hoặc Dijkstra.');}
  function setView(view) {S.view=['graph','tree','path'].includes(view)?view:'graph';document.querySelectorAll('[data-view]').forEach(b=>b.classList.toggle('active',b.dataset.view===S.view));scheduleRender();}
  async function loadDataset(id) {const g=await api('/api/graph?id='+encodeURIComponent(id));setGraph(g);const option=$('dataset').selectedOptions[0];$('dataset-description').textContent=option?.dataset.description||'Đồ thị có hướng, trọng số không âm.';}
  function attach() {
    $('run').addEventListener('click',safe(()=>run('dmm25')));$('run-chd').addEventListener('click',safe(()=>run('chd')));$('run-dijkstra').addEventListener('click',safe(()=>run('dijkstra')));$('pause').addEventListener('click',safe(()=>control(S.status==='paused'?'resume':'pause')));$('step').addEventListener('click',safe(()=>control('step')));$('stop').addEventListener('click',safe(()=>control('stop')));$('reset').addEventListener('click',safe(reset));
    $('dataset').addEventListener('change',safe(()=>loadDataset($('dataset').value)));$('mode').addEventListener('change',()=>{updateControls();renderMetrics();});$('source').addEventListener('change',()=>{S.comparison={dmm25:null,chd:null,dijkstra:null};if(S.result||S.events.length){S.result=null;S.events=[];S.runId=null;S.status='idle';renderEvents();}initDistances();renderAll();updateControls();});$('target').addEventListener('change',()=>selectVertex(Number($('target').value),false));$('search').addEventListener('keydown',e=>{if(e.key==='Enter')selectVertex(Number($('search').value));});$('find').addEventListener('click',()=>{if($('search').value!=='')selectVertex(Number($('search').value));});
    $('speed').addEventListener('input',()=>{$('speed-output').textContent=$('speed').value+' ms';});$('speed').addEventListener('change',safe(async()=>{if(!S.offline&&S.runId&&['running','paused'].includes(S.status))await control('speed');}));$('weights').addEventListener('change',scheduleRender);$('zoom-in').addEventListener('click',()=>zoom(1.25));$('zoom-out').addEventListener('click',()=>zoom(.8));$('fit').addEventListener('click',()=>{fit();scheduleRender();});
    $('import-button').addEventListener('click',()=>$('file-input').click());$('file-input').addEventListener('change',safe(async e=>{await importFile(e.target.files[0]);e.target.value='';}));document.querySelectorAll('[data-export]').forEach(b=>b.addEventListener('click',safe(()=>exportFile(b.dataset.export))));document.querySelectorAll('[data-view]').forEach(b=>b.addEventListener('click',()=>setView(b.dataset.view)));
    document.querySelectorAll('[data-tab]').forEach(b=>b.addEventListener('click',()=>{const isDistances=b.dataset.tab==='distances';$('distances-pane').hidden=!isDistances;$('events-pane').hidden=isDistances;document.querySelectorAll('[data-tab]').forEach(t=>{const active=t===b;t.classList.toggle('active',active);t.setAttribute('aria-selected',String(active));});}));$('prev-page').addEventListener('click',()=>{S.page=Math.max(0,S.page-1);renderTable();});$('next-page').addEventListener('click',()=>{S.page=Math.min(Math.ceil(S.graph.n/20)-1,S.page+1);renderTable();});
    const stage=$('graph-stage');stage.addEventListener('wheel',e=>{if(!S.graph)return;e.preventDefault();const r=stage.getBoundingClientRect();zoom(e.deltaY<0?1.12:.89,e.clientX-r.left,e.clientY-r.top);},{passive:false});stage.addEventListener('pointerdown',e=>{if(!S.graph||e.button!==0)return;const point=worldPoint(e);let node=null;for(const v of S.visible){const p=S.graph.positions[v];if(Math.hypot(p.x-point.x,p.y-point.y)<22){node=v;break;}}S.move={node,startX:e.clientX,startY:e.clientY,lastX:e.clientX,lastY:e.clientY,moved:false};stage.setPointerCapture(e.pointerId);});stage.addEventListener('pointermove',e=>{const move=S.move;if(!move)return;const dx=e.clientX-move.lastX,dy=e.clientY-move.lastY;move.lastX=e.clientX;move.lastY=e.clientY;if(Math.hypot(e.clientX-move.startX,e.clientY-move.startY)>3)move.moved=true;if(move.node!==null){const point=worldPoint(e);S.graph.positions[move.node]=point;}else{S.viewport.x+=dx;S.viewport.y+=dy;}scheduleRender();});stage.addEventListener('pointerup',e=>{const move=S.move;S.move=null;if(move?.node!==null&&move?.node!==undefined&&!move.moved)selectVertex(move.node,false);if(stage.hasPointerCapture(e.pointerId))stage.releasePointerCapture(e.pointerId);});stage.addEventListener('pointercancel',()=>S.move=null);stage.addEventListener('keydown',e=>{const node=e.target.closest('[data-node]');if(e.key==='Enter'&&node)selectVertex(Number(node.dataset.node),false);});
    let resizeTimer;window.addEventListener('resize',()=>{clearTimeout(resizeTimer);resizeTimer=setTimeout(()=>{fit();scheduleRender();},120);});window.addEventListener('beforeunload',()=>{closeStream();clearTimeout(S.timer);});
    setInterval(()=>{if(!S.offline&&['running','paused'].includes(S.status)){S.wall=performance.now()-S.startTime;$('wall-time').textContent=fmt(S.wall)+' ms';}},100);
  }
  async function init() {attach();const embedded=$('offline-data');if(embedded){try {const payload=JSON.parse(embedded.textContent);S.assets={css:Array.from(document.querySelectorAll('style')).map(s=>s.textContent).join('\n'),js:document.querySelector('script[data-app]')?.textContent||Array.from(document.scripts).find(s=>!s.src&&s.id!=='offline-data'&&s.textContent.includes('MIZCHI'))?.textContent||''};loadRecording(payload);$('dataset-description').textContent='Bản ghi ngoại tuyến · sự kiện từ phiên đã lưu.';}catch(error){notice('Không mở được bản ghi: '+error.message,true);}return;}try {const response=await api('/api/datasets');$('dataset').replaceChildren();for(const d of response.datasets){const option=new Option(d.name,d.id);option.dataset.description=d.description||'';$('dataset').add(option);}if(response.datasets.length){const query=new URLSearchParams(window.location.search),wanted=query.get('dataset'),selected=response.datasets.some(d=>d.id===wanted)?wanted:response.datasets[0].id;$('dataset').value=selected;S.algorithm=requestedAlgorithm(query.get('algorithm')||'dmm25');await loadDataset(selected);if(query.get('demo')==='live'){$('mode').value='controlled';$('speed').value='80';$('speed-output').textContent='80 ms';await safe(()=>run(S.algorithm))();}}else notice('Chưa có bộ đồ thị; hãy nhập TXT hoặc JSON.');}catch(error){$('dataset').replaceChildren(new Option('Không kết nối được máy chủ',''));notice(error.message+' Hãy kiểm tra máy chủ cục bộ.',true);}updateControls();}
  init();
})();
