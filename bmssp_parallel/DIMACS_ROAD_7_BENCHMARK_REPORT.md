# Báo Cáo Thực Nghiệm 7 Đồ Thị Giao Thông Đường Bộ Hoa Kỳ (9th DIMACS Challenge: `NY` → `CAL`)

**Ngày thực hiện:** 24/11/2026  
**Môi trường phần cứng:** AMD Ryzen 5 7640HS (6 nhân vật lý / 12 luồng logic, L3 Cache 16 MB), Windows 11, PowerShell  
**Trình biên dịch & Cờ tối ưu:** GCC `16.2.0` (`-std=c++20 -O3 -DNDEBUG -march=native -mcx16 -fopenmp`)  
**Mã nguồn đánh giá:** `dijkstra_sequential` (nguyên bản) đối sánh với `bmssp_parallel` (bản đã áp dụng 6 nhóm tối ưu cấu trúc dữ liệu/hệ thống, giữ nguyên 100% Algorithm 1–3 & Lemma 3.3 của Duan et al. STOC 2025, `use_gadget_transform = false`, không fallback Dijkstra)  
**Trình đo & Dữ liệu thô:** [`bench_dimacs_road.cpp`](benchmarks/bench_dimacs_road.cpp) | [`dimacs_road_7_results.csv`](dimacs_road_7_results.csv) | [`dimacs_road_7_vs_estimate.png`](dimacs_road_7_vs_estimate.png)

---

## 1. Minh Bạch Nguồn Gốc Dữ Liệu (Provenance) & Quy Trình Chuẩn Hóa

Do máy chủ gốc của Đại học Sapienza (`diag.uniroma1.it/challenge9` và `dis.uniroma1.it/~challenge9`) hiện ngừng phản hồi (HTTP 404/timeout), 7 bộ dữ liệu đầu tiên theo thứ tự quy mô tăng dần (`NY`, `BAY`, `COL`, `FLA`, `NW`, `NE`, `CAL`) được thu thập và tái tạo minh bạch từ các kho lưu trữ học thuật công khai của 9th DIMACS Implementation Challenge (TIGER/Line 2000):

| Mã | Khu vực địa lý | Nguồn dữ liệu thực tế (Provenance) | Trọng số | Đỉnh (`n`) | Cung thô | Cung sau chuẩn hóa (`m`) | Đỉnh tới được từ nguồn `0` |
|---|---|---|---|---:|---:|---:|---:|
| **`NY`** | New York City | Bản sao nguyên gốc `USA-road-t.NY.gr` (`lbwang95/QHL4CSP`) | Travel time (`t`) | 264.346 | 733.846 | **730.100** | 264.346 (100%) |
| **`BAY`** | San Francisco Bay Area | Bản sao nguyên gốc `USA-road-t.BAY.gr` (`lbwang95/QHL4CSP`) | Travel time (`t`) | 321.270 | 800.172 | **794.830** | 321.270 (100%) |
| **`COL`** | Colorado | Bản sao nguyên gốc `USA-road-t.COL.gr` (`lbwang95/QHL4CSP`) | Travel time (`t`) | 435.666 | 1.057.066 | **1.042.400** | 435.666 (100%) |
| **`FLA`** | Florida | Bản sao nguyên gốc mảng CSR `USA-road-d.FLA` (`sengorajkumar/gpu_graph_algorithms`) | Distance (`d`) | 1.070.376 | 2.712.798 | **2.687.902** | 1.070.376 (100%) |
| **`NW`** | Northwest USA | Tái tạo từ kho TIGER/Line 2000 `us_roads` của Dominik Schultes (`networks.skewed.de`, các bang `WA+OR+ID`, nối biên micro-độ, cắt `lon <= -115,999676°`, LCC) | Travel time (`t`) | 1.207.945 | 2.840.156 | **2.820.776** | 1.207.945 (100%) |
| **`NE`** | Northeast USA | Tái tạo từ kho TIGER/Line 2000 `us_roads` (`networks.skewed.de`, các bang `NY+NJ+PA+CT+MA`, nối biên micro-độ, cắt `lon >= -75,946816°`, LCC) | Travel time (`t`) | 1.524.453 | 3.887.270 | **3.857.376** | 1.524.453 (100%) |
| **`CAL`** | California & Nevada | Tái tạo từ kho TIGER/Line 2000 `us_roads` (`networks.skewed.de`, các bang `CA+NV+OR`, nối biên micro-độ, cắt `lat <= 42,379446°`, LCC) | Travel time (`t`) | 1.890.815 | 4.647.686 | **4.620.230** | 1.890.815 (100%) |

**Quy chuẩn tiền xử lý & phạm vi bộ đếm thời gian:**
- Trình nạp [`bench_dimacs_road.cpp`](benchmarks/bench_dimacs_road.cpp) chuyển chỉ số `1-based → 0-based`, loại bỏ khuyên (`u == v`) và khử cung song song trùng hướng giữ `min(w)` — đúng theo quy trình chuẩn hóa trên bài viết của `architecture-performance.fr` (số cung `m` của `NY`, `BAY`, `COL`, `FLA` khớp chính xác 100% tới từng đơn vị; `NW` khớp `2.820.776` so với `2.820.774`, sai khác 2 cung trên 2,82 triệu cung).
- **Phạm vi đo thời gian:** Tách riêng thời gian đọc/dựng `CSRGraph` (`parse_build_ms`). Mỗi cấu hình thuật toán chạy tuần tự `1 warmup + 5 mẫu đo`, lấy **trung vị (median)** thời gian giải SSSP thuần túy từ đỉnh nguồn `0` đến toàn bộ `n` đỉnh.
- **Kiểm chứng đúng đắn:** Đối chiếu toàn bộ `n` khoảng cách của từng lượt chạy với `dijkstra_sequential` độc lập ở ngưỡng dung sai `1e-9`.

---

## 2. Kết Quả Thực Nghiệm Chi Tiết

| Mã | Đỉnh (`n`) | Cung (`m`) | Bậc ra max | Đọc & dựng CSR (ms) | `Dijkstra 1T` (ms) | `BMSSP 1T` (ms) | `BMSSP 4T` (ms) | `BMSSP 8T` (ms) | `BMSSP 12T` (ms) | `BMSSP` tốt nhất (ms) | Tỷ số `BMSSP / Dijkstra` | Tăng tốc nội tại (`1T / Best`) | Sai số `MaxDiff` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| **`NY`** | 264.346 | 730.100 | 8 | 105,06 | **63,71** | 201,77 | 178,68 | 174,09 | **166,87** | **166,87** (`12T`) | `2,62×` | `1,21×` | `0.000` (`OK`) |
| **`BAY`** | 321.270 | 794.830 | 7 | 119,71 | **73,20** | 223,30 | 203,55 | **195,39** | 196,59 | **195,39** (`8T`) | `2,67×` | `1,14×` | `0.000` (`OK`) |
| **`COL`** | 435.666 | 1.042.400 | 8 | 153,17 | **101,79** | 269,06 | 238,61 | 241,02 | **236,77** | **236,77** (`12T`) | `2,33×` | `1,14×` | `0.000` (`OK`) |
| **`FLA`** | 1.070.376 | 2.687.902 | 8 | 235,23 | **226,74** | 638,99 | 577,49 | **541,95** | 563,35 | **541,95** (`8T`) | `2,39×` | `1,18×` | `0.000` (`OK`) |
| **`NW`** | 1.207.945 | 2.820.776 | 9 | 348,78 | **295,80** | 659,74 | 616,08 | **614,24** | 615,09 | **614,24** (`8T`) | `2,08×` | `1,07×` | `0.000` (`OK`) |
| **`NE`** | 1.524.453 | 3.857.376 | 9 | 464,77 | **445,99** | 936,50 | 803,36 | **758,88** | 760,91 | **758,88** (`8T`) | **`1,70×`** | **`1,23×`** | `0.000` (`OK`) |
| **`CAL`** | 1.890.815 | 4.620.230 | 7 | 607,30 | **482,93** | 1.077,89 | 985,64 | 914,01 | **891,43** | **891,43** (`12T`) | **`1,85×`** | **`1,21×`** | `0.000` (`OK`) |

- **Kiểm chứng chính xác tuyệt đối:** Cả 7 bộ dữ liệu × 4 mức luồng (`1T, 4T, 8T, 12T`) trên tổng cộng **`6.714.871` đỉnh** đều khớp chính xác với `dijkstra_sequential` (`MaxDiff = 0.000`, trạng thái `OK` 100%).
- **Hiệu quả so với mã trước tối ưu:** Trên `FLA` (`1,07M` đỉnh), thời gian của `BMSSP` giảm từ **`1.162,18 ms`** (bản repo trước khi áp dụng 6 nhóm tối ưu) xuống còn **`541,95 ms`** — tức **tăng tốc `2,14×`** ngay trên mạng giao thông đường bộ thực tế.

---

## 3. Đối Chiếu Ước Tính Lý Thuyết Trước Khi Chạy Và Số Đo Thực Nghiệm

| Mã | `Dijkstra 1T` (Ước tính) | `Dijkstra 1T` (Thực tế) | Đánh giá `Dijkstra` | `BMSSP Par` (Ước tính) | `BMSSP Par` (Thực tế) | Tỷ số `BMSSP/Dijk` (Ước tính) | Tỷ số `BMSSP/Dijk` (Thực tế) |
|---|---:|---:|---|---:|---:|---:|---:|
| **`NY`** | 45 – 60 ms | **63,71 ms** | Sát cận trên (+6,2%) | 95 – 125 ms | **166,87 ms** | ~1,9× – 2,2× | `2,62×` |
| **`BAY`** | 55 – 70 ms | **73,20 ms** | Sát cận trên (+4,6%) | 105 – 140 ms | **195,39 ms** | ~1,8× – 2,1× | `2,67×` |
| **`COL`** | 75 – 95 ms | **101,79 ms** | Sát cận trên (+7,1%) | 125 – 160 ms | **236,77 ms** | ~1,6× – 1,8× | `2,33×` |
| **`FLA`** | 200 – 260 ms | **226,74 ms** | **Trúng giữa khoảng ước tính** | 320 – 410 ms | **541,95 ms** | ~1,5× – 1,7× | `2,39×` |
| **`NW`** | 220 – 285 ms | **295,80 ms** | Sát cận trên (+3,8%) | 350 – 450 ms | **614,24 ms** | ~1,5× – 1,7× | `2,08×` |
| **`NE`** | 310 – 390 ms | **445,99 ms** | Cao hơn cận trên (+14,4%) | 460 – 580 ms | **758,88 ms** | ~1,4× – 1,6× | **`1,70×`** |
| **`CAL`** | 380 – 480 ms | **482,93 ms** | **Sát cận trên (+0,6%)** | 560 – 710 ms | **891,43 ms** | ~1,4× – 1,6× | **`1,85×`** |

### Phân tích nguyên nhân vi kiến trúc (Microarchitectural Analysis)

1. **Vì sao `Dijkstra (1T)` bám rất sát ước tính và cực nhanh trên mạng đường bộ?**
   - Khác với đồ thị ngẫu nhiên `Erdős–Rényi 1M` (`Dijkstra 1T = 746,77 ms`) hay `2M` (`1.642,68 ms`), trên mạng đường bộ `FLA 1,07M` và `CAL 1,89M`, `Dijkstra 1T` chỉ mất **`226,74 ms`** và **`482,93 ms`** (nhanh hơn **~3,3×** ở cùng số đỉnh!).
   - Nguyên nhân: các đỉnh giao lộ liền kề trong dữ liệu TIGER/Line được đánh số gần nhau theo không gian địa lý (*spatial locality* cao trên mảng `dist[]`), bậc ra rất nhỏ (`m/n ≈ 2,4 – 2,7`), và mặt sóng 2D phẳng giữ kích thước hàng đợi ưu tiên (binary min-heap) nhỏ hơn nhiều bậc độ lớn so với đồ thị expander, giúp phần lớn thao tác `sift-up/sift-down` nằm gọn trong L1/L2 cache.
   - Riêng trên `NE` (`m/n = 2,53`), mạng đường phố dày đặc của hành lang đô thị Đông Bắc (NYC–NJ–Philadelphia–Boston) làm mặt sóng rộng hơn và khi `n = 1,52M` vượt dung lượng L3 cache `16 MB`, chi phí cache miss của `Dijkstra` tăng lên `445,99 ms`.

2. **Vì sao `BMSSP tối ưu` chậm hơn khoảng ước tính ~25%–35% ở nhóm nhỏ (`NY..FLA`), nhưng thu hẹp mạnh khoảng cách với `Dijkstra` từ `2,67×` xuống `1,70× – 1,85×` ở `NE` và `CAL`?**
   - **Độ hẹp mặt sóng đường bộ:** Trên đồ thị phẳng bậc ~2,5, mỗi lát cắt khoảng cách trong `BMSSP` và mỗi bước Bellman-Ford $k$ vòng trong `FindPivots` chỉ mở rộng được một vành đai địa lý mỏng (vài chục đến vài trăm đỉnh), khiến rất nhiều nhánh đệ quy rơi dưới ngưỡng song song hóa `PAR_THRESHOLD = 512` và không đủ khối lượng công việc song song để bù hết chi phí phân tách đệ quy đa tầng của Algorithm 3.
   - **Quy luật hội tụ khi tăng quy mô (`n ≥ 1,5M`):** Đúng như dự báo lý thuyết trước khi chạy, khi quy mô tăng từ `NY/BAY` (`~300K` đỉnh) lên `NE` (`1,52M` đỉnh) và `CAL` (`1,89M` đỉnh):
     - Mặt sóng 2D mở rộng hơn giúp khả năng mở rộng đa luồng nội tại (`Self-Scale`) của `BMSSP` đạt **`1,21× – 1,23×`** (`NE`: `936,50 ms → 758,88 ms`; `CAL`: `1.077,89 ms → 891,43 ms`).
     - Đồng thời `Dijkstra` chịu áp lực tràn bộ nhớ đệm L3 mạnh hơn, kéo tỷ số **`BMSSP / Dijkstra` giảm đều đặn: `2,67×` (`BAY`) → `2,33×` (`COL`) → `2,08×` (`NW`) → `1,70×` (`NE`) / `1,85×` (`CAL`)**.

---

## 4. Lệnh Tái Lập Thực Nghiệm

Chạy từ thư mục gốc `C:\ttss` trên PowerShell:

```powershell
# 1. Biên dịch trình đo DIMACS .gr
g++ -std=c++20 -O3 -DNDEBUG -march=native -mcx16 -fopenmp `
    -Ibmssp_parallel/include -Ibmssp_parallel/include/baseline/external -Ibmssp_parallel/include/baseline/upstream/include `
    bmssp_parallel/benchmarks/bench_dimacs_road.cpp `
    bmssp_parallel/src/core/graph.cpp bmssp_parallel/src/baseline/bmssp_seq.cpp `
    bmssp_parallel/src/data_structure/d0_deque.cpp bmssp_parallel/src/data_structure/d1_skiplist.cpp `
    bmssp_parallel/src/data_structure/concurrent_frontier_d.cpp `
    bmssp_parallel/src/algorithm/find_pivots.cpp bmssp_parallel/src/algorithm/base_case.cpp bmssp_parallel/src/algorithm/bmssp.cpp `
    -o bmssp_parallel/benchmarks/bench_dimacs_road.exe

# 2. Chạy tuần tự cả 7 bộ dữ liệu và đối chứng Dijkstra Oracle
.\bmssp_parallel\benchmarks\bench_dimacs_road.exe bmssp_parallel\dimacs_road_7_results.csv init `
    NY:bmssp_parallel\data\dimacs_road\USA-road-t.NY.gr `
    BAY:bmssp_parallel\data\dimacs_road\USA-road-t.BAY.gr `
    COL:bmssp_parallel\data\dimacs_road\USA-road-t.COL.gr `
    FLA:bmssp_parallel\data\dimacs_road\USA-road-d.FLA.gr `
    NW:bmssp_parallel\data\dimacs_road\USA-road-t.NW.gr `
    NE:bmssp_parallel\data\dimacs_road\USA-road-t.NE.gr `
    CAL:bmssp_parallel\data\dimacs_road\USA-road-t.CAL.gr
```
