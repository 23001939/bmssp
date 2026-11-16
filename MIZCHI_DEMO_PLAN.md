# Kế hoạch xây dựng Demo Visualizer cho mizchi/check-arxiv-2504-17033

Ngày lập: 14/11/2026. Người yêu cầu: người dùng dự án BMSSP.

**Tiến độ tổng phạm vi demo mizchi: 100%.** Cả 6 mốc M1–M6 đã hoàn thành và kiểm chứng toàn diện ngày 15/11/2026.

---

## 1. Mục tiêu

Xây dựng demo visualizer cục bộ cho repo Rust `mizchi/check-arxiv-2504-17033` (commit `b3f173ca43d4052c0872d824be0bd61456ab7dd3`), lấy kiến trúc demo hiện tại của `lcs147/bmssp` C++ (`bmssp_visualizer/`) làm mẫu.

Demo mới hỗ trợ **3 thuật toán**: DMM+25, C-HD và Dijkstra — chạy trên cùng đồ thị, so sánh kết quả, phát trace trực tiếp (live animation) và xuất file xem lại.

## 2. Bối cảnh kỹ thuật

### 2.1. Demo hiện tại (lcs147 C++)

| Thành phần | Mô tả |
|---|---|
| `bmssp_visualizer/server.py` | Python stdlib HTTP server, quản lý Run, stream SSE, gọi native C++ subprocess |
| `bmssp_visualizer/native/runner.cpp` | C++ instrumented BMSSP: phát NDJSON trace qua stdout, chờ ACK `next` trên stdin |
| `bmssp_visualizer/native/dijkstra.cpp` | C++ Dijkstra binary heap, cùng giao thức trace/ACK |
| `bmssp_visualizer/dist/` | HTML + CSS + JS: Canvas/SVG render đồ thị, animation queue, SSE consumer, export offline |
| `bmssp_visualizer/data.py` | Sinh/parse dataset, verify result, oracle Dijkstra Python |
| `bmssp_visualizer/build_native.py` | Build C++ exe khi source thay đổi |
| Giao thức runner ↔ server | stdout: NDJSON 1 dòng/event (start, enter, pivots, relax, complete, exit, native_result). stdin: `next\n` để ACK. Server đọc stdout từng dòng, phát SSE tới browser |

### 2.2. Repo Rust cần demo (mizchi)

| Đặc điểm | Giá trị |
|---|---|
| Thuật toán | DMM+25 (`src/dmm25.rs`), C-HD (`src/chd.rs`), Dijkstra (`src/dijkstra.rs`) |
| Transform | DMM+25 có constant-degree gadget: N' = Σ max(deg(v),1), thường 2–33× n |
| Trace hiện tại | **Không có** — exe chỉ in kết quả cuối (distances + stats JSON) |
| Input | `Graph` struct trong Rust; không đọc file TXT trực tiếp (trừ `compare.exe` tự sinh) |
| Output | `(Vec<f64>, Stats)` — distances mảng f64 + thống kê |
| Test | 3000+ random nhỏ + 60 medium + grid/sparse, exact distances vs Dijkstra: PASS |
| License | README ghi MIT nhưng chưa có file LICENSE trong repo |

### 2.3. Khác biệt cốt lõi

1. **Không có trace**: Rust exe hiện chạy batch, không phát event từng bước → cần viết binary Rust mới có hook trace.
2. **3 thuật toán** thay vì 2: giao diện cần selector và bảng so sánh 3 cột.
3. **Transform**: DMM+25 chạy trên đồ thị đã transform (N' > n); giao diện hiện đồ thị gốc nhưng phải ghi chú N' thực tế.
4. **Rust build**: cần `cargo build --release` thay `g++`.

## 3. Chiến lược: hai tầng

- **Tầng 1 (batch demo + kết quả):** Wrapper Python gọi Rust exe chế độ batch (plain), trả distances + stats, so sánh 3 thuật toán. Không animation từng bước. → Hoàn thành nhanh, đủ để test/so sánh.
- **Tầng 2 (trace animation):** Viết `traced_runner.rs` chèn hook vào `dmm25::find_pivots`, `base_case`, `bmssp`, `relax` để phát NDJSON + ACK. → Animation live giống demo C++ hiện tại.

Triển khai tuần tự: Tầng 1 trước, Tầng 2 tiếp ngay sau.

## 4. Mốc công việc và trọng số

| Mốc | Trọng số | Tổng khi đạt | Nội dung chi tiết | Điều kiện hoàn thành |
|---|---:|---:|---|---|
| **M1. Hạ tầng + build** | 10% | 10% | **Đã xong 14/11/2026.** Tạo `bmssp_visualizer_mizchi/`; clone cục bộ `native_rust/` từ origin mizchi, detached đúng commit `b3f173ca43d4052c0872d824be0bd61456ab7dd3`; thêm batch adapter local `src/bin/batch_runner.rs`, `build_rust.py`, `runner.py` và README provenance. | **Đạt.** `build_rust.py` build release thành công; wrapper gọi/parse JSON của cả DMM+25, C-HD, Dijkstra. Smoke distances khớp `[0,1,0,3,6,null]`; `cargo test --release --test bmssp_family_test` pass 3/3. |
| **M2. Backend server** | 15% | 25% | **Đã xong 15/11/2026.** Tạo `server.py` và `data.py` tái dụng kiến trúc `bmssp_visualizer/`. Hỗ trợ 3 thuật toán: `dmm25`, `chd`, `dijkstra`. API endpoints: `/api/datasets`, `/api/graph`, `/api/graphs` (POST), `/api/runs` (POST tạo run batch), `/api/runs/ID` (GET kết quả), `/api/runs/ID/control`, `/api/runs/ID/export` (result/graph/csv/html). Verify kết quả bằng Dijkstra Python oracle độc lập. Thêm test suite `tests/test_server.py` (6/6 pass) và `tests/test_cross_algorithms.py` (5/5 pass). | **Đạt.** Server port 8766 hoạt động ổn định, 11/11 tests pass. Cả 3 thuật toán khớp 100% khoảng cách với Python Oracle trên multiple graphs. |
| **M3. Rust traced runner** | 25% | 50% | **Đã xong 15/11/2026.** Viết `src/bin/traced_runner.rs` trong bản fork Rust. Đọc TXT input (`n m` rồi `u v w`). Nhận arg: `INPUT SOURCE ALGORITHM trace\|plain [SAMPLE_EVERY]`. Với `trace`: phát NDJSON events tương thích giao thức demo C++ (start, enter, relax, complete, exit, native_result), chờ ACK `next` trên stdin. Với `plain`: chỉ phát native_result cuối. Hook đã chèn: DMM+25 (`dmm25_sssp_traced`), C-HD (`chd_sssp_traced`), Dijkstra heap. Khôi phục predecessor tree BFS chính xác IEEE-754. | **Đạt.** Traced runner build release; cả 3 thuật toán stream NDJSON + ACK, đo bù `control_wait_ms`. 15/15 tests visualizer pass, 11/11 cargo release tests pass. |
| **M4. Giao diện web** | 25% | 75% | **Đã xong dứt điểm 15/11/2026.** Triển khai `bmssp_visualizer_mizchi/dist/`: (a) Ba nút chạy riêng DMM+25 / C-HD / Dijkstra. (b) Panel tham số: hiển thị $N', M', k, t, L$, relax calls. (c) Bảng so sánh 3 dòng độc lập khớp Oracle, native compute time, kích thước transformed graph, tham số, chờ điều khiển. (d) Animation trace live SSE: Canvas/SVG render đồ thị, highlight relax, pivots, complete. (e) Điều khiển transport: Tạm dừng, từng bước, tiếp tục, nhịp sự kiện. (f) Xuất HTML offline tự chứa và phát lại trace ngoại tuyến. (g) Tiêu đề/nhãn tiếng Việt chuẩn học thuật. (h) Bổ sung launcher 1-click `start_demo.py` (`--port 8766`, `--dataset`, `--no-autoplay`, `--no-browser`, `--stop`, `--status`, quản lý server nền) và test launcher. | **Đạt.** Chrome headless CDP xác minh: Fast mode cả 3 thuật toán khớp Oracle (cost=11); Controlled mode C-HD nhận SSE, Pause đóng băng, Step tăng 1 bước, Resume hoàn tất; HTML offline mở lại và replay đạt; 0 lỗi JS. 19/19 visualizer unit tests pass. |
| **M5. Dataset + dữ liệu mẫu** | 10% | 85% | **Đã xong 15/11/2026.** (a) Tạo `data.py` bổ sung `weighted_graph_50()`, `graph_to_txt()`. (b) Tạo `data/`: `demo.txt/json`, `weighted_50.txt/json`, `weighted_100.txt/json`. (c) Cập nhật catalog `/api/datasets` trong `server.py`. (d) Tạo `exports/`: các bản xuất `demo.html`, `weighted_50.html`, `weighted_100.html` (nhúng sẵn cả bảng đối chiếu 3 dòng và trace replay) cùng ZIP `mizchi-demo-results.zip`. | **Đạt.** Cả 3 thuật toán chạy trơn tru, khớp 100% khoảng cách với Python Oracle trên cả 3 bộ dữ liệu. File HTML mở offline hoàn hảo. |
| **M6. Kiểm thử + tài liệu** | 15% | 100% | **Đã xong 15/11/2026.** (a) `tests/test_data.py`: kiểm tra parser, normalize, reachability, verify modes (6/6 pass). (b) `tests/test_random_differential.py`: fuzz vi sai 25 đồ thị ngẫu nhiên x 3 thuật toán Rust vs Python Oracle (PASS 100%). (c) Chuẩn hóa `tests/test_launcher.py` (5/5 pass). (d) Toàn bộ 27/27 visualizer unit tests PASS; 11/11 cargo release tests PASS. (e) Hoàn thiện `README.md` và `CONTRACT.md` đầy đủ provenance, cờ lệnh, API endpoints và disclaimers học thuật. (f) Cập nhật `PROJECT_MEMORY.md`, `MIZCHI_DEMO_PLAN.md`. | **Đạt.** Toàn bộ 27/27 bài test Python đạt PASS; 11/11 bài test Cargo release đạt PASS; tài liệu hoàn chỉnh, không còn hạng mục nào tồn đọng. |

## 5. Phân công subagent

| Mốc | Agent | Lý do |
|---|---|---|
| M1 (hạ tầng) | Root | Tạo thư mục, script build, wrapper — quen codebase |
| M2 (server) | Root | Tái dụng server.py hiện tại, cần hiểu giao thức chi tiết |
| M3 (Rust traced runner) | Root | Phần phức tạp nhất, sửa Rust source, cần kiểm soát chặt giao thức |
| M4 review giao diện | **Gemini 3.8 Flash High** | Review HTML/JS/CSS diff, đề xuất thay đổi cho selector 3 thuật toán, panel tham số |
| M5 (dataset) | Root | Sinh data + xuất file |
| M6 review test/docs | **Gemini 3.8 Flash High** | Review test coverage, README, CONTRACT |

## 6. Giao thức trace dự kiến (Rust → Python → Browser)

### 6.1. Rust traced_runner stdout (NDJSON)

```jsonc
// start — luôn phát đầu tiên
{"type":"start","n":100,"source":0,"algorithm":"dmm25","k":2,"t":5,"levels":3,"n_transformed":400,"sample_every":1}

// enter — vào lời gọi bmssp/base_case
{"type":"enter","level":2,"bound":null,"sources":[0]}

// pivots — FindPivots trả P
{"type":"pivots","level":2,"vertices":[3,7,12]}

// relax — cập nhật khoảng cách
{"type":"relax","level":1,"u":3,"v":5,"distance":7.0,"predecessor":3}

// complete — tập đỉnh hoàn thành tại lời gọi này
{"type":"complete","level":1,"vertices":[3,5,8],"bound":15.0}

// exit — thoát lời gọi bmssp/base_case
{"type":"exit","level":2,"vertices":[3,5,7,8,12]}

// native_result — kết quả cuối, phát không cần ACK
{"type":"native_result","distances":[0,3,5,null,...],"predecessors":[null,0,1,null,...],"compute_ms":4.53,"control_wait_ms":120.5,"k":2,"t":5,"levels":3,"n_transformed":400,"algorithm":"dmm25"}
```

### 6.2. stdin ACK

Sau mỗi event (trừ native_result), runner chờ dòng `next\n` trên stdin trước khi tiếp tục. Chế độ `plain` không phát event trung gian và không chờ ACK.

### 6.3. Vertex ID mapping

DMM+25 chạy trên đồ thị transform (N' đỉnh). Trace event phát vertex ID gốc (0..n-1) bằng cách ánh xạ ngược qua `Gadget::base[]`. Giao diện chỉ biết đồ thị gốc.

## 7. Cấu trúc thư mục

```
bmssp_visualizer_mizchi/
├── native_rust/                  ← bản fork repo Rust có trace hooks
│   ├── src/
│   │   ├── bin/traced_runner.rs  ← binary mới: đọc TXT, phát NDJSON, ACK
│   │   ├── dmm25.rs             ← thêm trace callback
│   │   ├── chd.rs               ← thêm trace callback
│   │   ├── dijkstra.rs          ← thêm trace callback
│   │   ├── common.rs            ← giữ nguyên + trace macro
│   │   ├── graph.rs             ← thêm parse TXT
│   │   └── lib.rs
│   ├── Cargo.toml               ← thêm serde_json, thêm [[bin]] traced_runner
│   ├── tests/
│   └── README.md                ← provenance, diff vs upstream, license note
├── dist/                         ← fork giao diện web
│   ├── index.html
│   ├── app.js
│   └── style.css
├── data/                         ← dataset TXT/JSON
├── exports/                      ← kết quả/HTML offline đã xuất
├── runs/                         ← phiên chạy runtime
├── tests/
│   ├── test_data.py
│   ├── test_server.py
│   └── test_runner.py
├── build_rust.py                 ← gọi cargo build --release
├── data.py                       ← sinh/parse dataset, verify, oracle
├── server.py                     ← HTTP server + SSE + run manager
├── start_demo.py                 ← khởi động server + mở browser
├── CONTRACT.md                   ← giao thức trace/API
└── README.md                     ← hướng dẫn sử dụng
```

## 8. Tái dụng vs viết mới

| Phần | Tái dụng từ demo C++ | Viết mới / chỉnh sửa |
|---|---|---|
| Engine Canvas/SVG render đồ thị | ✅ 100% | — |
| Animation queue, SSE consumer | ✅ 100% | — |
| UI layout, CSS | ✅ ~90% | Thêm selector 3 thuật toán, panel tham số DMM+25/C-HD |
| Server: Run lifecycle, SSE stream, export | ✅ ~80% | Thay lệnh gọi C++ → Rust, thêm thuật toán thứ 3 |
| data.py: parse, oracle, verify | ✅ ~70% | Thêm parser stats DMM+25/C-HD |
| build_native.py → build_rust.py | Tái dụng pattern | Viết mới cho cargo |
| Native runner | — | **Viết mới 100%**: `traced_runner.rs` |
| Dataset | Tái dụng pattern sinh đồ thị | Viết mới seed/config |
| Test | Tái dụng pattern test | Viết mới cho 3 thuật toán |
| CONTRACT.md | Tái dụng cấu trúc | Cập nhật cho 3 thuật toán + tham số mới |

## 9. Rủi ro và giới hạn

1. **License repo Rust**: README ghi MIT nhưng không có file LICENSE. Bản fork trong dự án ghi rõ provenance; không phân phối ngoài dự án cho đến khi upstream bổ sung văn bản license.

2. **DMM+25 chậm 14–121× so với Dijkstra**: Demo sẽ ghi disclaimer rõ ràng rằng đây là overhead thuật toán lý thuyết + trace, không phải benchmark. Thời gian animation ≠ thời gian tính toán.

3. **Constant-degree transform**: N' có thể lớn hơn n nhiều lần. Giao diện vẫn hiển thị đồ thị gốc n đỉnh; số liệu N', M, k, t, L nằm trong panel tham số.

4. **Trace hook thay đổi mã Rust**: Bản fork phải giữ test pass (`cargo test --release`). Mọi diff với upstream phải ghi trong `native_rust/README.md`.

5. **Test hữu hạn ≠ chứng minh tổng quát**: Giữ nguyên disclaimer như demo C++ hiện tại.

6. **Port mặc định**: Demo mizchi dùng port **8766** để không xung đột với demo C++ hiện tại (port 8765).

## 10. Quy tắc báo cáo tiến độ

- Mỗi lượt trả lời cập nhật **Tiến độ tổng phạm vi demo mizchi: X%**.
- % căn cứ trọng số mốc ở §4; chỉ tăng khi có hạng mục thực sự hoàn thành.
- Không báo 100% khi còn hạng mục bắt buộc chưa xong.
- Cập nhật tối đa khoảng 60 giây giữa các thông báo khi đang làm việc dài.
- Lưu trạng thái mốc trong file này và tóm tắt trong `PROJECT_MEMORY.md`.

## 11. Lệnh tái lập dự kiến

```powershell
# Build runner Rust
python bmssp_visualizer_mizchi/build_rust.py

# Chạy test
python -m unittest discover -s bmssp_visualizer_mizchi/tests -v

# Mở demo
python bmssp_visualizer_mizchi/start_demo.py --port 8766 --dataset weighted_50 --no-autoplay

# Mở browser
# http://127.0.0.1:8766/?dataset=weighted_50
```