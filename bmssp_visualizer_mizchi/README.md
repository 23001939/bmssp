# BMSSP Mizchi Rust Visualizer & Lab

Phòng thí nghiệm trực quan hóa thuật toán SSSP (Single-Source Shortest Paths) cục bộ dành cho repo Rust [`mizchi/check-arxiv-2504-17033`](https://github.com/mizchi/check-arxiv-2504-17033), khảo sát bài báo [arXiv:2504.17033v2](https://arxiv.org/abs/2504.17033) (*Breaking the Sorting Barrier for Directed Single-Source Shortest Paths*, Ran Duan et al., STOC 2025).

Ứng dụng hỗ trợ so sánh song song **3 thuật toán** trên cùng một đồ thị đầu vào:
1. **DMM+25:** Thuật toán BMSSP triển khai trung thực bài báo (Algorithms 1–3, cấu trúc khối Lemma 3.3 và biến đổi bậc hằng số chu trình trọng số 0 $N'$ đỉnh).
2. **C-HD:** Biến thể hop-constrained BMSSP với phân rã out-degree thành chuỗi độ dài $\Delta$.
3. **Dijkstra:** Cài đặt đối chứng bằng Rust Binary Heap chuẩn.

Mọi kết quả khoảng cách đều được kiểm chứng độc lập với **Python Dijkstra Oracle** (sai số số thực tuyệt đối $\le 10^{-9}$ hoặc đẳng thức số nguyên chính xác).

---

## 1. Provenance & Tuyên bố học thuật (Academic Disclaimers)

- **Mã nguồn gốc:** Thư mục `native_rust/` là bản clone cục bộ của `https://github.com/mizchi/check-arxiv-2504-17033.git`, pinned tại commit `b3f173ca43d4052c0872d824be0bd61456ab7dd3` (Kotaro Chikuba, 2026-09-23). Đây là nỗ lực tái hiện độc lập của bên thứ ba, không phải kho mã chính thức của 5 tác giả bài báo.
- **Giới hạn bản quyền (License):** README upstream ghi "MIT" nhưng checkout tại commit này **chưa có tệp `LICENSE` chính thức**. Mã nguồn chỉ phục vụ nghiên cứu và kiểm chứng nội bộ; không phân phối hoặc tái xuất bản ra ngoài khi chưa có văn bản cấp phép rõ ràng từ tác giả.
- **Hiệu năng thực tế so với lý thuyết:** Cận thời gian $\mathcal{O}(m \log^{2/3} n)$ của BMSSP là bước đột phá lý thuyết trong mô hình so sánh - cộng. Tuy nhiên trên CPU thực tế với kích thước đồ thị vừa và nhỏ, **Dijkstra thực thi nhanh hơn DMM+25 và C-HD từ vài lần đến hàng chục lần** do chi phí biến đổi bậc (gadget expansion $N' \gg n$), overhead cấu trúc khối `BlockDs`, phân mảnh bộ nhớ và trượt cache L1/L2. Thời gian animation/SSE không phản ánh tốc độ thuật toán thuần.
- **Tính đúng đắn:** Kết quả kiểm chứng (pass hàng nghìn đồ thị ngẫu nhiên và bộ dữ liệu mẫu) là bằng chứng thực nghiệm mạnh mẽ nhưng không thay thế cho chứng minh toán học hình thức về tính đúng đắn và cận amortized của toàn bộ codebase.

---

## 2. Cài đặt & Yêu cầu môi trường

- **Hệ điều hành:** Windows 10/11 (PowerShell).
- **Python:** $\ge$ 3.10 (khuyến nghị Python 3.11).
- **Rust toolchain:** `rustc` và `cargo` $\ge$ 1.80 (đã kiểm chứng ổn định trên Rust 1.100.0).

---

## 3. Hướng dẫn sử dụng

### 3.1. Khởi động 1-click (Khuyến nghị)

Chạy launcher tự động kiểm tra cổng 8766, khởi chạy server nền và mở trình duyệt:

```powershell
python bmssp_visualizer_mizchi/start_demo.py
```

Các tùy chọn dòng lệnh của `start_demo.py`:
- `--port 8766`: Chỉ định cổng HTTP (mặc định: 8766, tránh xung đột với visualizer C++ port 8765).
- `--dataset [demo|weighted_50|weighted_100]`: Nạp sẵn đồ thị khi mở.
- `--algorithm [dmm25|chd|dijkstra]`: Chọn thuật toán mặc định.
- `--no-browser`: Chỉ khởi động server nền, không mở tab trình duyệt.
- `--no-autoplay`: Mở giao diện nhưng không tự động chạy demo live.
- `--status`: Kiểm tra trạng thái tiến trình server đang chạy.
- `--stop`: Dừng server nền một cách an toàn.

### 3.2. Chạy server ở chế độ tiền cảnh (Foreground)

```powershell
python bmssp_visualizer_mizchi/server.py --port 8766
```

### 3.3. Xem ngoại tuyến không cần máy chủ (Offline Mode)

Mở trực tiếp các tệp HTML tự chứa trong thư mục `bmssp_visualizer_mizchi/exports/` bằng bất kỳ trình duyệt nào:
- `exports/demo.html`: Đồ thị minh họa 18 đỉnh (có trọng số 0, cạnh song song, chu trình và đỉnh unreachable).
- `exports/weighted_50.html`: Đồ thị lưới 50 đỉnh, 120 cạnh có hướng, trọng số 1–15.
- `exports/weighted_100.html`: Đồ thị 100 đỉnh, 240 cạnh có hướng, trọng số 1–20.

Các bản xuất HTML độc lập nhúng sẵn CSS, JS, đồ thị, toàn bộ trace sự kiện (để phát lại tương tác) và bảng so sánh 3 dòng của cả 3 thuật toán.

---

## 4. Kiến trúc hệ thống

```
bmssp_visualizer_mizchi/
├── native_rust/              # Bản clone cục bộ repo mizchi/check-arxiv-2504-17033
│   ├── src/bin/batch_runner.rs  # Adapter batch plain output JSON
│   ├── src/bin/traced_runner.rs # Traced runner stream NDJSON + ACK stdin
│   └── src/trace.rs             # Định nghĩa module sự kiện (start, enter, relax, complete, exit)
├── dist/                     # Giao diện Web tĩnh thuần túy (Buildless Vanilla JS)
│   ├── index.html            # Cấu trúc UI: 3 nút chạy, panel N'/M'/k/t/L, bảng đối chiếu 3 dòng
│   ├── app.js                # Quản lý đồ thị SVG/Canvas, SSE consumer, transport control, offline replay
│   └── style.css             # Giao diện thiết kế học thuật chuẩn mực
├── data/                     # Dữ liệu đồ thị chuẩn hóa (.txt và .json)
│   ├── demo.txt / demo.json
│   ├── weighted_50.txt / weighted_50.json
│   └── weighted_100.txt / weighted_100.json
├── exports/                  # Kết quả xuất tự chứa (HTML offline, CSV, JSON, trace NDJSON, ZIP)
├── tests/                    # Bộ kiểm thử tự động toàn diện (27+ tests)
│   ├── test_data.py          # Kiểm thử parser, generator, reachability, Dijkstra Oracle
│   ├── test_server.py        # Kiểm thử REST API endpoints và vòng đời Run
│   ├── test_runner.py        # Kiểm thử traced runner, SSE stream, transport control (pause/step/resume)
│   ├── test_launcher.py      # Kiểm thử launcher 1-click start_demo.py
│   ├── test_cross_algorithms.py # Đối chiếu chéo 3 thuật toán trên các cấu hình đồ thị chuẩn
│   └── test_random_differential.py # Fuzz test vi sai 25 đồ thị ngẫu nhiên vs Python Oracle
├── data.py                   # Data models, generators, graph normalize, independent Dijkstra Oracle
├── runner.py                 # Subprocess manager gọi batch_runner và traced_runner
├── server.py                 # HTTP Server (cổng 8766), REST API, SSE streaming, xuất file
├── start_demo.py             # Launcher 1-click thông minh
└── CONTRACT.md               # Đặc tả hợp đồng dữ liệu REST API, SSE events và schema JSON
```

---

## 5. Chạy kiểm thử tự động

### Kiểm thử Python:
```powershell
python -m unittest discover -s bmssp_visualizer_mizchi/tests -v
```

### Kiểm thử Cargo Rust:
```powershell
cd bmssp_visualizer_mizchi/native_rust
cargo test --release
```
