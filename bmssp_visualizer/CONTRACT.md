# Integration contract

Local-only BMSSP visualizer. Python stdlib backend, buildless HTML/CSS/JS frontend, instrumented copy of upstream C++ WC implementation. Upstream checkout stays unchanged.

## Graph

`{id,name,n,source,target,edges:[{id,u,v,w}],positions:[{x,y}],metadata:{...}}`

Vertex IDs are 0..n-1, edge IDs stable integers. Positions optional on input, normalized to approximately 0..1000 coordinates. Input parser also accepts legacy `{n,edges:[[u,v,w],...]}` and plain TXT (`n m`, then `u v w`). Finite nonnegative weights only. Parallel edges supported. Output unreachable distances/predecessors use null.

## HTTP

- GET `/api/datasets` -> `{datasets:[{id,name,n,m,description}]}`.
- GET `/api/graph?id=ID` -> graph object.
- POST `/api/graphs` with `{graph:object}` or `{text:string,format:"txt"|"json",name?:string}` -> `{id,graph}`.
- POST `/api/runs` with `{graph_id,source,algorithm?:"bmssp"|"dijkstra",mode:"controlled"|"fast",delay_ms:0..1000}` -> `{run_id,status,algorithm}`. Default algorithm is bmssp. Controlled mode trace is real and ACK-gated. Fast runs produce results without animation waits and are labeled separately.
- GET `/api/runs/ID/events` -> SSE `data: JSON\n\n` events. Each event `{run_id,seq,type,...}`; sequence is monotonic per run. SSE starts from beginning for new connections; optional query after=SEQ.
- GET `/api/runs/ID` -> `{run_id,status,graph_id,source,result,events_count,sampled,error}`.
- POST `/api/runs/ID/control` with `{action:"pause"|"resume"|"step"|"stop"|"speed",delay_ms?:number}` -> run status.
- GET `/api/runs/ID/export?kind=graph|result|trace|csv|html` -> downloadable file. HTML must work offline with embedded graph/result/events and frontend assets. Large traces can be sampled with explicit metadata.
- Errors -> HTTP 400/404/409/500 `{error:string}`. Validation returns understandable Vietnamese text.

## Trace and result

Trace event types: `start`, `enter`, `pivots`, `relax`, `complete`, `exit`, `result`, `error`, `stopped`. UI must ignore unknown fields/types safely.

- start: `{n,source,algorithm,...}`
- enter/exit: `{level,bound,sources?:[vertex IDs],vertices?:[vertex IDs],...}`; nonfinite bound uses null.
- pivots: `{level,vertices:[original vertex IDs],...}`
- relax: `{level,u,v,edge_id?:int,distance:number,predecessor:u,...}`
- complete: `{level,vertices:[original vertex IDs],distances?:[number|null],...}`. Do not infer completion from relax alone.
- result: `{result:RESULT}` added by backend after verification. C++ native terminal payload can be normalized by backend.

RESULT: `{algorithm,source,distances:[number|null],predecessors:[int|null],predecessor_edges:[int|null],reachable:[bool],compute_ms:number|null,wall_ms:number,control_wait_ms:number,verified:bool,verification:{...},trace:{sampled:bool,events:number},...}`. `compute_ms` of a controlled trace is not a clean benchmark. Include a clearly labeled native no-trace measurement for fast runs. Recover predecessor edge IDs deterministically from endpoints/weights; verify no predecessor cycles and path costs.

Correction after review: verification has `comparison_mode`, `integer_exact_domain`, `distance_exact_ok`, `distance_tolerance_ok`, `path_exact_ok`, `path_tolerance_ok`. `verified` means both distance and path checks pass in the selected mode. Auto selects exact for nonnegative integer weights whose `(n-1)*max_weight <= 2^53`; otherwise tolerance is 1e-9. Exact here means binary64 equality, not a proof about arbitrary real arithmetic. BMSSP `predecessor_origin` is `solver` after validation, or `exact_edge_reconstruction`; no epsilon-based reconstruction is accepted. Baseline upstream verification is a separate historical diagnostic.

Extension 14/11/2026: result also has `algorithm_id` (`bmssp`/`dijkstra`), `native_compute_ms` (alias for native compute duration) and `graph_hash` (SHA256 over ordered input n/edges, excludes display positions and source). Compare only identical graph_hash/source. Dijkstra no-trace single-run time is native_compute_ms; BMSSP original no-hook single-run time is baseline_ms. Neither is a statistically stable benchmark. New graph `weighted_100` has 100 vertices, 240 directed edges, weights 1..20, seed20261114, source0/target99.

## Offline frontend

Backend serves `dist/index.html`, `dist/app.js`, `dist/style.css`. Exported HTML embeds same assets plus `<script id="offline-data" type="application/json">{graph,result,events}</script>`. Frontend detects offline-data and provides replay, selection/path visualization and local exports without calling backend.

## Ownership

- Root: server.py, data.py, export generation, integration, progress/memory.
- runner agent: native/*, build_native.py; document CLI/event details in native/README.md. Confirm CLI with root promptly.
- frontend agent: dist/* only. Follow this contract; responsive Vietnamese UI, no CDN, diagram/SVG/Canvas code native.
- QA agent: tests/* and README.md only; may request fixes from root, do not modify other files.
