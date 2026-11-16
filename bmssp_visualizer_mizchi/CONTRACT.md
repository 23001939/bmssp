# Giao thức Hợp đồng Dữ liệu (CONTRACT.md) — BMSSP Mizchi Visualizer

**Dự án:** `bmssp_visualizer_mizchi` (Port mặc định: 8766)  
**Phiên bản:** M6 Final Specification  
**Cập nhật:** 15/11/2026  
**Provenance:** Fork nghiên cứu từ repo Rust `mizchi/check-arxiv-2504-17033`, commit pinned `b3f173ca43d4052c0872d824be0bd61456ab7dd3`, rustc 1.100.0.

---

## 1. Kiến trúc hệ thống

```
Trình duyệt Web (M4) / HTTP Client
       │
       ▼ (HTTP / REST API & Server-Sent Events - Port 8766)
bmssp_visualizer_mizchi/server.py (App & Handler)
       │
       ├── Independent Python Dijkstra Oracle (data.py: verify_result)
       │
       ▼ (Subprocess stdio: input.txt -> traced_runner.exe / batch_runner.exe)
native_rust/src/bin/
       ├── traced_runner.rs (Stream NDJSON events qua stdout, nhận ACK `next\n` qua stdin)
       └── batch_runner.rs  (Fast mode: chỉ in 1 dòng JSON native_result)
```

Hỗ trợ 3 thuật toán trên cùng đồ thị:
1. `dmm25`: Thuật toán BMSSP triển khai theo bài báo (Algorithms 1–3, cấu trúc khối Lemma 3.3, cycle gadget $N'$ đỉnh).
2. `chd`: Biến thể C-HD hop-constrained với phân rã out-degree thành chuỗi độ dài $\Delta$.
3. `dijkstra`: Đối chứng độc lập bằng Rust Binary Heap chuẩn.

---

## 2. Danh mục REST API Endpoints

| Phương thức | Đường dẫn | Tham số / Body | Mã phản hồi | Mô tả |
|---|---|---|---|---|
| `GET` | `/api/datasets` | Không | `200 OK` | Lấy danh mục đồ thị có sẵn (`demo`, `weighted_50`, `weighted_100`). |
| `GET` | `/api/graph` | `?id=<graph_id>` | `200 OK`, `404` | Lấy dữ liệu đồ thị JSON đầy đủ gồm tọa độ render và danh sách cạnh. |
| `POST` | `/api/graphs` | JSON graph hoặc `{text, format, name}` | `200 OK`, `400` | Tải lên đồ thị mới (TXT hoặc JSON), cấp phát ID `upload_<hex>`. |
| `POST` | `/api/runs` | `{graph_id, source, algorithm, mode, delay}` | `200 OK`, `400`, `409` | Khởi tạo lần chạy mới (`mode="controlled"` hoặc `"fast"`). |
| `GET` | `/api/runs/<id>` | `?wait=true\|false` | `200 OK`, `404` | Lấy snapshot/kết quả thực thi. Hỗ trợ chờ kết thúc với `wait=true`. |
| `GET` | `/api/runs` | `?graph_id=<id>` (tùy chọn) | `200 OK` | Lấy lịch sử các lần chạy trên đồ thị để so sánh 3 thuật toán. |
| `POST` | `/api/runs/<id>/control` | `{action, delay}` | `200 OK`, `400`, `404` | Điều khiển transport: `pause`, `resume`, `step`, `stop`, `speed`. |
| `GET` | `/api/runs/<id>/events` | `?after=<seq>` | `200 OK` (SSE) | Stream Server-Sent Events sự kiện thời gian thực từ runner native. |
| `GET` | `/api/runs/<id>/export` | `?kind=result\|graph\|csv\|html\|trace` | `200 OK`, `400`, `404` | Xuất dữ liệu lần chạy theo các định dạng tiêu chuẩn. |
| `GET` | `/`, `/index.html`, `/app.js`, `/style.css` | Không | `200 OK` | Phục vụ giao diện web tĩnh trong `dist/`. |

---

## 3. Cấu trúc Graph JSON Schema

```json
{
  "id": "demo",
  "name": "Mạng minh họa · 18 đỉnh (mizchi)",
  "n": 18,
  "source": 0,
  "target": 16,
  "edges": [
    {"id": 0, "u": 0, "v": 1, "w": 4.0},
    {"id": 1, "u": 0, "v": 5, "w": 2.0}
  ],
  "positions": [
    {"x": 100.0, "y": 95.0},
    {"x": 290.0, "y": 95.0}
  ],
  "metadata": {
    "description": "Đồ thị minh họa 18 đỉnh: có trọng số 0, cạnh song song và đỉnh unreachable."
  }
}
```

Ràng buộc:
- Đỉnh `u, v` phải là số nguyên trong nửa khoảng `[0, n)`.
- Trọng số `w` phải hữu hạn, không âm (`w >= 0.0`).
- Cạnh song song (parallel edges) được hỗ trợ đầy đủ và phân biệt qua trường `id`.

---

## 4. Giao thức Sự kiện Lần vết (Traced Events Protocol)

Tiến trình Rust `traced_runner` giao tiếp qua stdio theo chuẩn NDJSON (1 dòng 1 JSON) và cơ chế đồng bộ ACK:
1. Rust phát sự kiện NDJSON -> Python server nhận, lưu vào hàng đợi và phát SSE tới trình duyệt.
2. Python gửi `next\n` vào stdin của Rust để cho phép chạy bước tiếp theo.
3. Cơ chế tính bù thời gian chờ (`control_wait_ms`) đảm bảo đo chính xác `compute_ms` thực của thuật toán.

### Các loại sự kiện chuẩn:

```jsonc
// 1. Khởi động lần chạy
{"type": "start", "n": 18, "source": 0, "algorithm": "dmm25", "k": 2, "t": 4, "levels": 3, "n_transformed": 45, "sample_every": 1}

// 2. Vào lời gọi đệ quy
{"type": "enter", "level": 1, "bound": 15.0, "sources": [0]}

// 3. FindPivots trả về tập pivot P
{"type": "pivots", "level": 1, "vertices": [2, 5]}

// 4. Relax cạnh
{"type": "relax", "level": 1, "u": 0, "v": 5, "distance": 2.0, "predecessor": 0, "edge_id": 1}

// 5. Hoàn tất lô đỉnh
{"type": "complete", "level": 1, "vertices": [0, 5], "bound": 4.0}

// 6. Thoát mức đệ quy
{"type": "exit", "level": 1, "vertices": [0, 5]}

// 7. Kết quả native cuối cùng (không cần ACK)
{"type": "native_result", "n": 18, "m": 30, "distances": [0.0, 4.0, ...], "predecessors": [null, 0, ...], "compute_ms": 0.452, "control_wait_ms": 1250.0, "stats": {...}}
```

---

## 5. Cấu trúc Kết quả Thực thi (Run Result Schema)

```json
{
  "run_id": "8f3a12bc90de4567",
  "algorithm": "DMM+25 (BMSSP Rust · N_reduced gadget)",
  "algorithm_id": "dmm25",
  "source": 0,
  "n": 18,
  "m": 30,
  "distances": [0.0, 4.0, 7.0, 9.0, 12.0, 2.0, 3.0, 5.0, 6.0, 8.0, 7.0, 6.0, 6.0, 8.0, 9.0, 10.0, 11.0, null],
  "predecessors": [null, 0, 1, 2, 3, 0, 5, 6, 7, 8, 5, 6, 11, 12, 13, 10, 14, null],
  "predecessor_edges": [null, 0, 3, 5, 7, 1, 10, 12, 14, 16, 11, 13, 19, 21, 23, 20, 24, null],
  "reachable": [true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, false],
  "compute_ms": 0.165,
  "wall_ms": 1.285,
  "control_wait_ms": 0.0,
  "mode": "controlled",
  "trace": {
    "sampled": false,
    "events": 274,
    "sample_every": 1
  },
  "stats": {
    "n_transformed": 45,
    "m_transformed": 60,
    "k": 2,
    "t": 4,
    "levels": 3,
    "relax_calls": 80
  },
  "verification": {
    "ok": true,
    "oracle": "Independent Dijkstra (Python heapq)",
    "comparison_mode": "exact",
    "integer_exact_domain": true,
    "distance_exact_ok": true,
    "wrong_vertices": 0,
    "exact_mismatches": 0,
    "max_abs_error": 0.0
  },
  "verified": true,
  "provenance": {
    "repo": "https://github.com/mizchi/check-arxiv-2504-17033",
    "commit": "b3f173ca43d4052c0872d824be0bd61456ab7dd3",
    "adapter": "src/bin/traced_runner.rs",
    "rustc": "1.100.0",
    "algorithm": "dmm25",
    "execution_mode": "controlled"
  }
}
```

---

## 6. Định dạng Tệp Xuất (Exports)

1. **`result.json`** (`kind=result`): JSON đầy đủ gồm kết quả, thống kê $k, t, L, N', M'$, xác minh và xuất xứ.
2. **`graph.json`** (`kind=graph`): JSON cấu trúc đồ thị ban đầu cùng tọa độ hiển thị.
3. **`trace.ndjson`** (`kind=trace`): Dòng sự kiện NDJSON hoàn chỉnh từ lúc khởi chạy đến kết thúc.
4. **`distances.csv`** (`kind=csv`): Tệp CSV 3 cột:
   ```csv
   vertex,distance,reachable
   0,0.0,true
   1,4.0,true
   17,,false
   ```
5. **`demo.html`** (`kind=html`): Tệp HTML độc lập tự chứa (self-contained offline report), nhúng toàn bộ dữ liệu JSON trong thẻ `<script id="offline-data" type="application/json">` với cơ chế escape an toàn chống XSS. Có khả năng phát lại sự kiện và hiển thị bảng so sánh 3 dòng độc lập mà không cần kết nối tới máy chủ.
