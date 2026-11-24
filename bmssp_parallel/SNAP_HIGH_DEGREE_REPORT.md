# Báo Cáo Thực Nghiệm 5 Đồ Thị Thực Tế Bậc Trung Bình – Cao Stanford SNAP (`34,5K` → `1,79M` Đỉnh)

**Ngày thực hiện:** 25/11/2026  
**Môi trường phần cứng:** AMD Ryzen 5 7640HS (6 nhân vật lý / 12 luồng logic, L3 Cache 16 MB), Windows 11, PowerShell  
**Trình biên dịch & Cờ tối ưu:** GCC `16.2.0` (`-std=c++20 -O3 -DNDEBUG -march=native -mcx16 -fopenmp`)  
**Mã thuật toán:** Giữ nguyên **100%** mã `bmssp_parallel` và `dijkstra_sequential` trên nhánh `parallel-bmssp` / `Viet` (commit `4796b93`), không chỉnh sửa bất kỳ dòng mã thuật toán nào  
**Trình đo & Dữ liệu thô:** [`bench_snap_highdeg.cpp`](benchmarks/bench_snap_highdeg.cpp) | [`snap_highdeg_results.csv`](snap_highdeg_results.csv) | [`snap_high_degree_vs_dijkstra.png`](snap_high_degree_vs_dijkstra.png)

---

## 1. Danh Mục 5 Bộ Dữ Liệu Thực Tế Stanford SNAP & Quy Chuẩn Chuẩn Hóa

Toàn bộ 5 bộ dữ liệu thực tế có bậc trung bình từ `~11` đến `~19` được tải trực tiếp từ **Stanford Large Network Dataset Collection (SNAP)**:

| Thứ tự theo quy mô | Bộ dữ liệu | Loại mạng thực tế | Đỉnh (`n`) | Cung thô | Cung sau khử khuyên/trùng (`m`) | Bậc TB (`m/n`) | Bậc ra max | Đỉnh tới được từ nguồn `0` |
|---:|---|---|---:|---:|---:|---:|---:|---:|
| 1 | **`cit-HepPh`** | Trích dẫn Vật lý năng lượng cao ArXiv (DAG) | 34.546 | 421.578 | **421.534** | `12,20` | 411 | 20.507 (59,36%) |
| 2 | **`soc-Slashdot0811`** | Cộng đồng công nghệ Slashdot | 77.360 | 905.468 | **828.161** *(77.307 khuyên)* | `10,71` | 2.507 | 77.360 (100,00%) |
| 3 | **`web-BerkStan`** | Liên kết Web `berkeley.edu` & `stanford.edu` | 685.230 | 7.600.595 | **7.600.595** | `11,09` | 249 | 459.831 (67,11%) |
| 4 | **`soc-Pokec`** | Mạng xã hội trực tuyến Pokec Slovakia | 1.632.803 | 30.622.564 | **30.622.564** | **`18,75`** | 8.763 | 1.504.295 (92,13%) |
| 5 | **`wiki-topcats`** | Liên kết bài viết Wikipedia Top Categories | 1.791.489 | 28.511.807 | **28.508.141** | **`15,91`** | 3.907 | 1.791.489 (100,00%) |

*(Ghi chú chuẩn hóa trên `soc-Slashdot0811`: tệp gốc có `905.468` dòng chứa `77.307` cạnh tự nối `u → u`; sau khi loại khuyên theo chuẩn đồ thị đơn có hướng còn đúng `828.161` cung, nên bậc trung bình thực tế là `10,71`).*

- **Trọng số cạnh:** Gán trọng số nguyên dương tất định chuẩn **GAP Benchmark Suite** (`w(u, v) ∈ [1, 255]` cố định qua hàm băm `SplitMix64(u_raw, v_raw)`).
- **Phạm vi đo:** Tách riêng thời gian đọc file & dựng `CSRGraph`. Chạy tuần tự `1 warmup + 5 mẫu đo`, lấy **trung vị (median)** thời gian giải SSSP thuần túy và kiểm chứng toàn bộ `n` khoảng cách với `dijkstra_sequential` (`tol = 1e-9`).

---

## 2. Bảng Kết Quả Thực Nghiệm Toàn Diện (Từ `34,5K` Đến `1,79M` Đỉnh)

| Bộ dữ liệu | Đỉnh (`n`) | Bậc TB | `Dijkstra 1T` (ms) | `BMSSP 1T` (ms) | `BMSSP 2T` (ms) | `BMSSP 4T` (ms) | `BMSSP 8T` (ms) | `BMSSP 12T` (ms) | `BMSSP` tốt nhất | Tốc độ so với `Dijkstra` (`Dijk / Best`) | Tăng tốc nội tại | Oracle `MaxDiff` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| **`cit-HepPh`** | 34.546 | `12,20` | **6,41** | 19,10 | 19,75 | 18,20 | 21,20 | 19,50 | 18,20 ms (`4T`) | `0,35×` *(kém `2,84×`)* | `1,05×` | `0.000` (`OK`) |
| **`soc-Slashdot0811`** | 77.360 | `10,71` | **53,06** | 72,95 | 80,62 | 73,23 | 66,20 | 64,86 | 64,86 ms (`12T`) | `0,82×` *(kém `1,22×`)* | `1,12×` | `0.000` (`OK`) |
| **`web-BerkStan`** | 685.230 | `11,09` | 299,01 | 464,93 | 447,04 | 403,07 | **291,25** | 299,11 | **291,25 ms (`8T`)** | **`1,03×` (vượt nhẹ / hòa vốn)** | **`1,60×`** | `0.000` (`OK`) |
| **`wiki-topcats`** | 1.791.489 | `15,91` | 2.219,93 | 2.535,15 | 1.906,69 | 1.421,54 | 1.336,99 | **1.315,10** | **1.315,10 ms (`12T`)** | **`1,69×` nhanh hơn** | **`1,93×`** | `0.000` (`OK`) |
| **`soc-Pokec`** | 1.632.803 | `18,75` | 2.474,98 | 2.520,72 | 1.859,25 | 1.442,74 | 1.462,64 | **1.351,73** | **1.351,73 ms (`12T`)** | **`1,83×` nhanh hơn** | **`1,86×`** | `0.000` (`OK`) |

- **Kiểm chứng chính xác tuyệt đối:** Toàn bộ **25 cấu hình `BMSSP`** trên cả 5 đồ thị thực tế (tổng cộng **`4.221.428` đỉnh** và **`67.980.995` cung có hướng**) đều khớp chính xác với `dijkstra_sequential` (`MaxDiff = 0.000`, trạng thái `OK` 100%).

---

## 3. Phân Tích Đường Cong Hội Tụ Và Điểm Cắt (Crossover Point) Thực Tế

Đặt 5 bộ dữ liệu theo trật tự quy mô tăng dần cho thấy một **quy luật thực nghiệm đơn điệu rất rõ ràng** của hệ số `Speedup vs. Dijkstra`:

$$\mathbf{0,35\times}\;(\text{cit-HepPh}) \;\longrightarrow\; \mathbf{0,82\times}\;(\text{soc-Slashdot0811}) \;\longrightarrow\; \mathbf{1,03\times}\;(\text{web-BerkStan}) \;\longrightarrow\; \mathbf{1,69\times}\;(\text{wiki-topcats}) \;\longrightarrow\; \mathbf{1,83\times}\;(\text{soc-Pokec})$$

1. **Vùng quy mô nhỏ nằm trọn trong bộ nhớ đệm L2/L3 (`n < 100K` — `cit-HepPh` & `soc-Slashdot0811`):**
   - Trên `cit-HepPh` (`20.507` đỉnh tới được, toàn bộ mảng khoảng cách chỉ chiếm `~270 KB` nằm gọn trong L2 cache `1 MB`), `Dijkstra` tuần tự chỉ mất **`6,41 ms`**. Ở thang vài mili-giây này, chi phí cố định khởi tạo `BMSSPContext`, phân tách đệ quy và đánh thức vùng OpenMP của `BMSSP` (`18,20 ms`) lớn hơn thời gian tính toán thực tế.
   - Tuy nhiên, chỉ cần tăng quy mô gấp đôi lên `soc-Slashdot0811` (`77.360` đỉnh tới được, mạng xã hội *small-world*), thời gian của `Dijkstra` đã nhảy vọt hơn `8,2 lần` (`6,41 ms → 53,06 ms`), trong khi `BMSSP` chỉ tăng `3,5 lần` (`18,20 ms → 64,86 ms`), thu hẹp khoảng cách từ `0,35×` lên **`0,82×`** (chỉ còn chênh `11,8 ms`).

2. **Điểm cắt vượt qua `Dijkstra` trên dữ liệu thực tế (`~460K` đỉnh tới được — `web-BerkStan`):**
   - Trên `web-BerkStan` (`n = 685.230`, `459.831` đỉnh tới được, bậc `11,09`), các trang Web thuộc cùng thư mục tên miền `berkeley.edu` / `stanford.edu` có tính cụm cục bộ rất cao (thuận lợi lớn cho bộ nhớ đệm của `Dijkstra`, chỉ mất `299,01 ms` cho `7,6 triệu` cung).
   - Dù vậy, nhờ quy mô đủ lớn để khai thác đa luồng (`Self-Scale = 1,60×`, giảm từ `464,93 ms` ở `1T` xuống **`291,25 ms` ở `8T`**), `BMSSP` **chính thức bắt kịp và vượt nhẹ `Dijkstra` (`1,03×`)**.

3. **Vùng bứt phá ở quy mô hàng triệu đỉnh & bậc `15 – 20` (`wiki-topcats` & `soc-Pokec`):**
   - Khi quy mô vượt ngưỡng L3 cache `16 MB` (`1,63M – 1,79M` đỉnh) kết hợp bậc trung bình `15,91 – 18,75` (`28,5M – 30,6M` cung), binary heap của `Dijkstra` bị nghẽn băng thông RAM/cache (`2.220 – 2.475 ms`), trong khi `BMSSP` mở rộng nội tại gần gấp đôi (`1,86× – 1,93×`) và **nhanh hơn `Dijkstra` từ `1,69×` đến `1,83×`**.

---

## 4. Lệnh Tái Lập Toàn Bộ 5 Bộ Dữ Liệu

```powershell
python -X utf8 bmssp_parallel/benchmarks/download_snap_highdeg.py

.\bmssp_parallel\benchmarks\bench_snap_highdeg.exe bmssp_parallel\snap_highdeg_results.csv init `
    cit-HepPh:bmssp_parallel\data\snap\cit-HepPh.txt `
    soc-Slashdot0811:bmssp_parallel\data\snap\soc-Slashdot0811.txt `
    web-BerkStan:bmssp_parallel\data\snap\web-BerkStan.txt `
    wiki-topcats:bmssp_parallel\data\snap\wiki-topcats.txt `
    soc-Pokec:bmssp_parallel\data\snap\soc-Pokec.txt
```
