# Báo Cáo Thực Nghiệm Đồ Thị Thực Tế Bậc Cao Stanford SNAP (`wiki-topcats` & `soc-Pokec`)

**Ngày thực hiện:** 25/11/2026  
**Môi trường phần cứng:** AMD Ryzen 5 7640HS (6 nhân vật lý / 12 luồng logic, L3 Cache 16 MB), Windows 11, PowerShell  
**Trình biên dịch & Cờ tối ưu:** GCC `16.2.0` (`-std=c++20 -O3 -DNDEBUG -march=native -mcx16 -fopenmp`)  
**Mã thuật toán:** Giữ nguyên **100%** mã `bmssp_parallel` và `dijkstra_sequential` trên nhánh `Viet` (commit `4796b93`), không chỉnh sửa thêm bất kỳ dòng mã thuật toán nào; chỉ bổ sung trình đọc danh sách cạnh SNAP [`bench_snap_highdeg.cpp`](benchmarks/bench_snap_highdeg.cpp)  
**Dữ liệu thô & Biểu đồ:** [`snap_highdeg_results.csv`](snap_highdeg_results.csv) | [`snap_high_degree_vs_dijkstra.png`](snap_high_degree_vs_dijkstra.png)

---

## 1. Nguồn Gốc Dữ Liệu (Provenance) & Quy Chuẩn Thực Nghiệm

Hai bộ dữ liệu công khai có bậc trung bình trong vùng `15 – 20` được tải trực tiếp từ **Stanford Large Network Dataset Collection (SNAP)**:

| Bộ dữ liệu | Nguồn tải trực tiếp | Đỉnh (`n`) | Cung thô | Cung chuẩn hóa (`m`) | Bậc trung bình (`m/n`) | Bậc ra cực đại | Nguồn (`src`) | Đỉnh tới được |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| **`wiki-topcats`** | `https://snap.stanford.edu/data/wiki-topcats.txt.gz` | 1.791.489 | 28.511.807 | **28.508.141** | **`15,91`** | 3.907 | `0` | 1.791.489 (**100,00%**) |
| **`soc-Pokec`** | `https://snap.stanford.edu/data/soc-pokec-relationships.txt.gz` | 1.632.803 | 30.622.564 | **30.622.564** | **`18,75`** | 8.763 | `0` | 1.504.295 (**92,13%**) |

**Quy chuẩn tiền xử lý & bộ đếm thời gian:**
- Trình nạp [`bench_snap_highdeg.cpp`](benchmarks/bench_snap_highdeg.cpp) ánh xạ chỉ số đỉnh liên tục `0..n-1` theo đúng thứ tự định danh gốc, loại khuyên (`u == v`), khử cung trùng hướng và gán trọng số nguyên dương tất định theo quy chuẩn **GAP Benchmark Suite** (`w(u, v) ∈ [1, 255]` qua hàm băm `SplitMix64(u_raw, v_raw)` — xác định 100% và giống hệt cho cả hai thuật toán).
- Thời gian đọc file và dựng `CSRGraph` (`2,59s` cho `wiki-topcats`, `3,34s` cho `soc-Pokec`) được tách riêng hoàn toàn.
- Mỗi cấu hình chạy tuần tự `1 warmup + 5 mẫu đo`, báo cáo **trung vị (median)** và đối chứng toàn bộ `n` khoảng cách với `dijkstra_sequential` độc lập ở ngưỡng dung sai `1e-9`.

---

## 2. Kết Quả Thực Nghiệm Chi Tiết

| Bộ dữ liệu | Bậc TB (`m/n`) | `Dijkstra 1T` (ms) | `BMSSP 1T` (ms) | `BMSSP 2T` (ms) | `BMSSP 4T` (ms) | `BMSSP 8T` (ms) | `BMSSP 12T` (ms) | Tốc độ vượt `Dijkstra` (`Dijk / Best`) | Tăng tốc nội tại (`1T / 12T`) | Sai số `MaxDiff` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| **`wiki-topcats`** | `15,91` | 2.219,93 | 2.535,15 | **1.906,69** | **1.421,54** | **1.336,99** | **1.315,10** | **`1,69×` nhanh hơn** | **`1,93×`** | `0.000` (`OK`) |
| **`soc-Pokec`** | `18,75` | 2.474,98 | 2.520,72 | **1.859,25** | **1.442,74** | **1.462,64** | **1.351,73** | **`1,83×` nhanh hơn** | **`1,86×`** | `0.000` (`OK`) |

- **Kiểm chứng chính xác tuyệt đối:** Toàn bộ 10 cấu hình `BMSSP` (`1T, 2T, 4T, 8T, 12T` × 2 đồ thị) trên tổng cộng **`3.424.292` đỉnh** và **`59.130.705` cung thực tế** đều đạt **`MaxDiff = 0.000` (`OK`)**.
- **Vượt `Dijkstra` ngay từ 2 luồng:** Trên cả `wiki-topcats` và `soc-Pokec`, chỉ cần **2 luồng** (`1.906,69 ms` và `1.859,25 ms`) `BMSSP` đã vượt qua `Dijkstra 1T` (`2.219,93 ms` và `2.474,98 ms`). Ở mức **12 luồng**, `BMSSP` rút ngắn **`904,83 ms`** trên `wiki-topcats` (`1,69×`) và **`1.123,25 ms`** (hơn 1,12 giây mỗi lượt truy vấn SSSP) trên `soc-Pokec` (`1,83×`).

---

## 3. Kiểm Chứng Giả Thuyết Về Bậc Trung Bình: Đối Sánh Cùng Quy Mô ~1,6M – 1,9M Đỉnh

Đặt 3 đồ thị thực tế có cùng quy mô khoảng `1,63 – 1,89 triệu` đỉnh cạnh nhau để thấy rõ tác động quyết định của mật độ cạnh (`m/n`) và cấu trúc liên kết:

| Đồ thị thực tế | Đỉnh (`n`) | Cung (`m`) | Bậc TB (`m/n`) | `Dijkstra 1T` (ms) | `BMSSP 1T` (ms) | `BMSSP 12T` (ms) | Kết quả đối sánh |
|---|---:|---:|---:|---:|---:|---:|---|
| **DIMACS `CAL`** *(Đường bộ)* | 1.890.815 | 4.620.230 | **`2,44`** | **482,93** | 1.077,89 | 891,43 | `Dijkstra` nhanh hơn `1,85×` |
| **SNAP `wiki-topcats`** *(Wikipedia)* | 1.791.489 | 28.508.141 | **`15,91`** | 2.219,93 | 2.535,15 | **1.315,10** | **`BMSSP` nhanh hơn `1,69×`** |
| **SNAP `soc-Pokec`** *(Mạng xã hội)* | 1.632.803 | 30.622.564 | **`18,75`** | 2.474,98 | 2.520,72 | **1.351,73** | **`BMSSP` nhanh hơn `1,83×`** |

**Phân tích định lượng:**
1. Khi chuyển từ mạng đường bộ `CAL` (`4,62M` cung, bậc `2,44`) sang mạng xã hội `soc-Pokec` (`30,62M` cung, bậc `18,75`) ở cùng tầm ~`1,7M` đỉnh, **số cung tăng gấp `6,63 lần`**:
   - **`Dijkstra (1T)` bị chậm đi `5,12 lần`** (`482,93 ms → 2.474,98 ms`) do số thao tác cập nhật/vun đống nhị phân tăng theo $\mathcal{O}(m \log n)$ kèm truy cập bộ nhớ phân tán trên liên kết xã hội gây trượt L3 cache nặng.
   - **`BMSSP (12T)` chỉ tăng thời gian đúng `1,52 lần`** (`891,43 ms → 1.351,73 ms` cho lượng cạnh lớn gấp `6,63 lần`!). Ngay cả ở **1 luồng đơn (`BMSSP 1T`)**, thời gian của `BMSSP` cũng chỉ tăng `2,34 lần` (`1.077,89 ms → 2.520,72 ms`) và tiệm cận sát nút `Dijkstra 1T` (`2.520,72 ms` so với `2.474,98 ms`, chênh lệch chỉ `1,8%`).
2. Nguyên nhân cốt lõi: cấu trúc hàng đợi khối phân tầng $\mathcal{D}$ (Lemma 3.3) của `BMSSP` quản lý theo **tập đỉnh chưa hoàn tất** (số đỉnh $n$ không đổi), còn toàn bộ `28,5 – 30,6 triệu` cung được quét tuần tự trên mảng kề `CSR` và chia tải hiệu quả qua 12 luồng (đạt hệ số tự tăng tốc nội tại **`1,86× – 1,93×`**, cao hơn hẳn mức `1,07× – 1,23×` trên mạng đường bộ phẳng).

---

## 4. Lệnh Tái Lập

```powershell
# Tải dữ liệu SNAP (nếu chưa có trong bmssp_parallel/data/snap/)
python -X utf8 bmssp_parallel/benchmarks/download_snap_highdeg.py

# Chạy đo tuần tự và kiểm chứng Dijkstra Oracle
.\bmssp_parallel\benchmarks\bench_snap_highdeg.exe bmssp_parallel\snap_highdeg_results.csv init `
    wiki-topcats:bmssp_parallel\data\snap\wiki-topcats.txt `
    soc-Pokec:bmssp_parallel\data\snap\soc-Pokec.txt
```
