# Báo Cáo Profiling Từng Pha Parallel BMSSP (`FindPivots`, `BaseCase`, `D.Pull`, `D.BatchPrepend`, `Edge Relaxation`, `insert_batch`)

- **Ngày đo:** 21/11/2026 (Asia/Saigon)
- **Nhánh mã nguồn:** `Viet` (cục bộ, sau khi vá lỗi lost-wakeup deadlock trong `parallel_sample_sort` của [primitives.hpp](file:///c:/ttss/bmssp_parallel/include/core/primitives.hpp#L256-L270))
- **Công cụ profiling:** Opt-in compile-time `#ifdef BMSSP_ENABLE_PROFILING` tại [profiler.hpp](file:///c:/ttss/bmssp_parallel/include/core/profiler.hpp) và [profile_parallel_bmssp.cpp](file:///c:/ttss/bmssp_parallel/benchmarks/profile_parallel_bmssp.cpp) (0% overhead trên bản production).
- **Môi trường:** Windows / PowerShell, GCC `16.2.0` (`-std=c++20 -O3 -march=native -mcx16 -fopenmp -DBMSSP_ENABLE_PROFILING`), CPU `12` luồng logic, `ConcurrentFrontierD`.
- **Phương pháp:** 1 warmup + 3 lượt chạy tuần tự (trung bình `Avg ms` và `%` tổng `total_solve_ns`), đối chiếu 100% khoảng cách với Oracle `dijkstra_sequential` (`tol = 1e-9`) ở mọi lượt chạy. Các pha được đo **không chồng lấn** (`total = sum(phases) + other`).

---

## 1. Kết Luận Chẩn Đoán Chính (Trả Lời Câu Hỏi Giới Hạn Nằm Ở Đâu)

| Yếu tố nghi ngờ | Kết luận từ số liệu Profiling thực tế | Mức độ ảnh hưởng |
|---|---|---|
| **1. Tuần tự (`D.Pull`)** | **THỦ PHẠM SỐ 1.** Chiếm **`30,1% – 44,0%`** tổng thời gian ở 8 luồng (`109,5 ms` trên Dense 200K; `97,2 ms` trên Sparse 200K; `56,6 ms` trên Hub 100K) và **hoàn toàn không giảm** khi tăng từ 1T → 4T → 8T. | **RẤT CAO (Chặn trần Amdahl)** |
| **2. Đồng bộ / Overhead tạo thread (`FindPivots`)** | **THỦ PHẠM SỐ 2 (Phát hiện bất ngờ).** `FindPivots` bị **chậm ngược từ `1,34×` đến `2,08×`** khi tăng 1T → 8T (`Sparse 200K`: `31,36 ms` → `65,09 ms`, chiếm `28,44%` ở 8T). Nguyên nhân trực tiếp: `find_pivots.cpp` gọi `parallel_gather()` ở mỗi bước `step`, mà commit TSan `4570d58` vừa đổi `parallel_gather` sang **tạo/hủy `std::thread` của OS ở mỗi lần gọi**! | **RẤT CAO (Ăn mất gần hết speedup)** |
| **3. `Edge Relaxation` & `insert_batch` (Song song)** | **HOẠT ĐỘNG TỐT.** Trên `Dense-20 200K`, `Edge Relaxation` giảm `121,90 → 39,39 ms` (**`3,09×`**) và `insert_batch` giảm `113,06 → 48,27 ms` (**`2,34×`**, riêng phần song song chỉ còn `15,23 ms`). Trên `Sparse 200K`, cả hai pha đều tăng tốc **`2,53× – 2,59×`**. | **TỐT (Không phải nút thắt chính)** |
| **4. Mất cân bằng tải (Load Imbalance `max/avg`)** | **NHẸ** trên đồ thị lớn (`Edge Relaxation`: `1,10× – 1,38×`; `insert_batch`: `1,16× – 1,50×`) nhờ edge-level partitioning. Chỉ trên `2D Grid 300×300` có ít lời gọi vượt cutoff 2048 (`11 / 12.886` calls, `par_edges = 2,25%` ở `FindPivots`). | **THẤP** (trừ 2D Grid frontier hẹp) |
| **5. `BaseCase`, `D.init`, `D.BatchPrepend`** | **RẤT NHỎ.** `BaseCase` chỉ chiếm **`0,08% – 2,05%`** trên Sparse/Dense/Hub (và `5,01%` trên 2D Grid); `D.init` chiếm `0,20% – 2,05%`; `D.BatchPrepend` chiếm `0,06% – 3,33%`. | **KHÔNG ĐÁNG KỂ** |

> **Phát hiện phụ quan trọng:** Nhờ chạy profiling, đã phát hiện và sửa một lỗi **Lost-Wakeup Deadlock** trong `parallel_sample_sort()` của [primitives.hpp](file:///c:/ttss/bmssp_parallel/include/core/primitives.hpp#L256-L270) (từ commit `4570d58`), nơi `done.store(true)` và `queue_cv.notify_all()` từng nằm ngoài `queue_mtx`.

---

## 2. Bảng Phân Rã Chi Tiết 4 Đồ Thị (`1T` vs `4T` vs `8T`)

### 2.1. `Sparse Random 200K` (`n = 200.000, m = 1.000.000`)

| Pha | 1T (ms) | 1T (%) | 4T (ms) | 4T (%) | 8T (ms) | 8T (%) | Tỷ lệ `1T/8T` | Ghi chú (8T) |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| **`D.Pull` (serial)** | 96,93 | 35,73% | 96,25 | 46,06% | **97,17** | **42,46%** | 1,00× | 2.251 calls/run — nút thắt #1 |
| **`FindPivots` (total)** | 31,36 | 11,56% | 44,60 | 21,34% | **65,09** | **28,44%** | **0,48×** | `par=47,96ms` (chậm ngược do `parallel_gather` spawn `std::thread`), `forest=8,72ms`, `imb=1,41×` |
| **`insert_batch` (flush D)** | 64,95 | 23,95% | 26,24 | 12,56% | **25,05** | 10,95% | **2,59×** | 286.413 items; `par_est=16,34ms`, `seq=8,71ms`, `imb=1,46×` |
| **`Edge Relaxation` (pure)** | 55,69 | 20,53% | 25,11 | 12,02% | **22,03** | 9,62% | **2,53×** | `par_calls=47/2251`, `par_edges=91,87%`, `imb=1,38×` |
| `K sort` (`sample_sort`) | 2,86 | 1,06% | 1,27 | 0,61% | 1,47 | 0,64% | 1,95× | 3 calls/run |
| `BaseCase` (serial) | 0,99 | 0,36% | 0,83 | 0,40% | 0,88 | 0,39% | 1,13× | 1.960 calls/run |
| `D.init` (serial) | 0,55 | 0,20% | 0,43 | 0,21% | 0,46 | 0,20% | 1,20× | 292 calls/run |
| `D.BatchPrepend` (serial) | 0,26 | 0,09% | 0,19 | 0,09% | 0,25 | 0,11% | 1,04× | 3 calls/run |
| `Other` (recursion/glue) | 17,67 | 6,51% | 14,04 | 6,72% | 16,43 | 7,18% | 1,08× | gather K, bookkeeping |
| **Tổng (`total_solve`)** | **271,25** | **100%** | **208,97** | **100%** | **228,83** | **100%** | **1,19×** | 8T chậm hơn 4T vì `FindPivots` tăng +20,5 ms |

### 2.2. `Dense-20 Random 200K` (`n = 200.000, m = 4.000.000`)

| Pha | 1T (ms) | 1T (%) | 4T (ms) | 4T (%) | 8T (ms) | 8T (%) | Tỷ lệ `1T/8T` | Ghi chú (8T) |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| **`D.Pull` (serial)** | 118,38 | 27,62% | 113,57 | 35,56% | **109,46** | **39,10%** | 1,08× | 505 calls/run — nút thắt #1 |
| **`FindPivots` (total)** | 47,07 | 10,98% | 61,24 | 19,17% | **63,13** | **22,55%** | **0,75×** | `par=48,29ms`, `forest=7,54ms`, `par_edges=95,36%`, `imb=1,25×` |
| **`insert_batch` (flush D)** | 113,06 | 26,38% | 60,35 | 18,90% | **48,27** | 17,24% | **2,34×** | 508.838 items; `par_est=15,23ms`, `seq=33,04ms`, `imb=1,35×` |
| **`Edge Relaxation` (pure)** | 121,90 | 28,44% | 61,24 | 19,17% | **39,39** | 14,07% | **3,09×** | `par_calls=49/505`, `par_edges=95,46%`, `imb=1,25×` |
| `K sort` (`sample_sort`) | 11,31 | 2,64% | 4,66 | 1,46% | 4,17 | 1,49% | 2,71× | 3 calls/run |
| `D.init` (serial) | 1,17 | 0,27% | 1,28 | 0,40% | 1,06 | 0,38% | 1,10× | 197 calls/run |
| `D.BatchPrepend` (serial) | 0,61 | 0,14% | 0,62 | 0,19% | 0,67 | 0,24% | 0,91× | 3 calls/run |
| `BaseCase` (serial) | 0,22 | 0,05% | 0,24 | 0,07% | 0,22 | 0,08% | 1,00× | 309 calls/run |
| `Other` (recursion/glue) | 14,84 | 3,46% | 16,19 | 5,07% | 13,57 | 4,85% | 1,09× | gather K, bookkeeping |
| **Tổng (`total_solve`)** | **428,56** | **100%** | **319,39** | **100%** | **279,95** | **100%** | **1,53×** | `Edge Relax` + `insert_batch` giảm 147 ms |

### 2.3. `Hub 100K` (`n = 100.000, deg(0) = 50.000, m = 450.000`)

| Pha | 1T (ms) | 1T (%) | 4T (ms) | 4T (%) | 8T (ms) | 8T (%) | Tỷ lệ `1T/8T` | Ghi chú (8T) |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| **`D.Pull` (serial)** | 60,29 | 38,47% | 58,76 | 42,66% | **56,58** | **43,96%** | 1,07× | 2.046 calls/run |
| **`FindPivots` (total)** | 16,63 | 10,61% | 29,42 | 21,36% | **30,18** | **23,45%** | **0,55×** | `par=22,30ms`, `par_edges=90,23%`, `imb=1,31×` |
| **`Edge Relaxation` (pure)** | 23,53 | 15,02% | 13,33 | 9,68% | **9,96** | 7,74% | **2,36×** | `par_calls=25/2046`, `par_edges=90,43%`, `imb=1,26×` |
| **`insert_batch` (flush D)** | 18,50 | 11,80% | 9,78 | 7,10% | **8,25** | 6,41% | **2,24×** | 94.052 items; `par_est=3,49ms`, `seq=4,76ms`, `imb=1,50×` |
| `K sort` (`sample_sort`) | 16,10 | 10,28% | 6,63 | 4,82% | 5,80 | 4,51% | **2,78×** | 4 calls/run |
| `D.BatchPrepend` (serial) | 5,44 | 3,47% | 4,49 | 3,26% | 4,28 | 3,33% | 1,27× | 4 calls/run |
| `BaseCase` + `D.init` | 6,47 | 4,13% | 5,70 | 4,14% | 5,28 | 4,10% | 1,23× | 1.843 / 204 calls |
| `Other` | 9,74 | 6,22% | 9,63 | 6,99% | 8,37 | 6,50% | 1,16× | gather K, bookkeeping |
| **Tổng (`total_solve`)** | **156,70** | **100%** | **137,74** | **100%** | **128,70** | **100%** | **1,22×** | `D.Pull` + `FindPivots` = **67,4%** tổng thời gian |

### 2.4. `2D Grid 300×300` (`n = 90.000, m = 358.800`)

| Pha | 1T (ms) | 1T (%) | 4T (ms) | 4T (%) | 8T (ms) | 8T (%) | Tỷ lệ `1T/8T` | Ghi chú (8T) |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| **`D.Pull` (serial)** | 21,51 | 29,93% | 21,74 | 30,32% | **21,11** | **30,09%** | 1,02× | 12.886 calls/run |
| **`insert_batch` (flush D)** | 15,75 | 21,91% | 15,84 | 22,08% | **15,44** | **22,01%** | 1,02× | **100% chạy ở nhánh `seq`** (`15,44ms`) vì frontier nhỏ |
| **`FindPivots` (total)** | 7,15 | 9,95% | 9,05 | 12,61% | **9,97** | **14,21%** | 0,72× | `par_edges` chỉ đạt **2,25%** |
| **`Edge Relaxation` (pure)** | 13,23 | 18,41% | 10,91 | 15,22% | **9,83** | 14,01% | 1,35× | `par_calls=11/12886`, `par_edges=46,87%` |
| `BaseCase` (serial) | 3,67 | 5,11% | 3,71 | 5,18% | 3,51 | 5,01% | 1,05× | 11.568 calls/run |
| `D.init` + `K sort` + `BatchPrepend` | 1,06 | 1,48% | 1,09 | 1,50% | 1,09 | 1,56% | 0,97× | Không đáng kể |
| `Other` | 9,49 | 13,21% | 9,38 | 13,08% | 9,20 | 13,11% | 1,03× | Overhead gọi đệ quy 12.886 lần |
| **Tổng (`total_solve`)** | **71,86** | **100%** | **71,71** | **100%** | **70,16** | **100%** | **1,02×** | Thiếu độ song song do frontier lưới 2D hẹp |

---

## 3. Lộ Trình Tối Ưu Tiếp Theo Dựa Trên Bằng Chứng Profiling (Không Đoán Mò)

Từ số liệu trên, hai việc mang lại hiệu quả lớn nhất (chiếm **62% – 71%** tổng thời gian ở 8 luồng) là:

1. **Sửa ngay `FindPivots` (`22,5% – 28,4%` tổng thời gian, đang bị chậm ngược 2× ở 8T):**
   - Trong [find_pivots.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/find_pivots.cpp#L213), thay `parallel_gather(ws.thread_buffers, next_active)` (hiện đang tạo/hủy `std::thread` của OS ở mỗi bước `step` sau commit `4570d58`) bằng gom trực tiếp bên trong vùng `#pragma omp parallel` (tính prefix-sum `offsets` trên 8 phần tử + copy song song ngay trong OpenMP team sẵn có, **0 lần tạo `std::thread` mới**). Dự kiến đưa `FindPivots (8T)` trên `Sparse 200K` từ `65 ms` xuống dưới `15 ms`!
2. **Tối ưu `D.Pull` (`35,6% – 44,0%` tổng thời gian — nút thắt tuần tự lớn nhất):**
   - Trong [concurrent_frontier_d.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/concurrent_frontier_d.cpp#L82-L120), `pull()` đang gọi `deduplicate_min_dist` (`std::sort` theo `v`) lặp lại trong vòng `while`, rồi lại `std::sort(merged_buf_)` theo `dist`, rồi đẩy phần dư ngược về `D0`/`D1`. Có thể thay `deduplicate_min_dist` bằng bảng epoch/hash $O(1)$ theo `v` (tránh `std::sort` thừa trong vòng `while`) và dùng `std::nth_element` thay cho full `std::sort` khi chỉ cần tách top-$M$ phần tử nhỏ nhất.
3. **Không cần ưu tiên `BaseCase` hay `D.BatchPrepend`:** Profiling đã chứng minh chúng chỉ chiếm `0,1% – 3,3%` trên đồ thị lớn.

---

## 4. Kiểm Chứng Sau Khi Tối Ưu `FindPivots` (24/11/2026 — Linux GCC 13.3.0)

### 4.1. Thay đổi mã nguồn
- Tại `bmssp_parallel/src/algorithm/find_pivots.cpp`: bỏ lời gọi `parallel_gather(ws.thread_buffers, next_active)` nằm ngoài khối OpenMP (vốn tạo/hủy `std::thread` mới của OS ở mỗi bước `step`).
- Khai báo sẵn `gather_offsets` ngoài vòng `for (step = 1..k)` và gom trực tiếp bên trong khối `#pragma omp parallel` đang mở sẵn bằng `#pragma omp barrier` $\to$ `#pragma omp single` tính offset + `resize` $\to$ các luồng OpenMP copy song song vào `next_active`.

### 4.2. Kiểm chứng tính đúng đắn (Release CTest + Dijkstra Oracle)
- **Môi trường:** Linux x86-64, GCC `13.3.0`, CMake + Ninja, flags `-std=c++20 -O3 -march=native -mcx16 -Wall -Wextra -fopenmp`, CPU `12` luồng logic.
- **Kết quả CTest (`4/4 PASS` trong `1,11s`):**
  - `test_person1`: PASS (`0,01s`)
  - `test_d_stress`: PASS (`0,75s`)
  - `verify_parallel_bmssp`: PASS (`0,03s`)
  - `test_parallel_edge_cases`: PASS (`0,32s`)
- **Đối chiếu Oracle trong `profile_parallel_bmssp`:** 100% lượt đo trên 4 đồ thị × `{1T, 4T, 8T}` khớp `dijkstra_sequential` (`tol = 1e-9`).

### 4.3. Đối chiếu hiệu năng Trước vs Sau (cùng máy Linux, cùng build flags `-DBMSSP_ENABLE_PROFILING`)

*(1 warmup + trung bình 3 lượt chạy tuần tự, `ConcurrentFrontierD`.)*

**Riêng pha `FindPivots` ở 8 luồng (8T):**

| Đồ thị | `FindPivots` Trước (8T) | `FindPivots` Sau (8T) | Nhánh `par` (Trước → Sau) | Mức giảm `FindPivots` |
|---|---:|---:|---:|---:|
| **Sparse Random 200K** (`m=1M`) | 36,54 ms | **18,23 ms** | `20,21 ms` → **`7,00 ms`** (`2,89×`) | **-50,1%** |
| **Dense-20 Random 200K** (`m=4M`) | 44,80 ms | **28,88 ms** | `27,33 ms` → **`16,79 ms`** (`1,63×`) | **-35,5%** |
| **Hub 100K** (`deg0=50K`) | 12,70 ms | **9,11 ms** | `7,40 ms` → **`4,67 ms`** (`1,58×`) | **-28,3%** |
| **2D Grid 300×300** (`m=359K`) | 3,88 ms | 3,92 ms | `0,68 ms` → `0,77 ms` | ~không đổi (`par_edges = 2,25%`) |

**Tổng thời gian `total_solve` và tỷ lệ tự tăng tốc `1T → 8T`:**

| Đồ thị | `1T → 4T → 8T` **Trước** (ms) | `1T → 4T → 8T` **Sau** (ms) | Speedup `1T → 8T` (Trước → Sau) | Tỷ trọng `D.Pull` ở 8T (Sau) |
|---|---|---|---|---:|
| **Sparse Random 200K** | `144,08 → 133,46 → 136,24` | `124,86 → 109,56 →` **`99,45`** | `1,06×` → **`1,26×`** | 51,10% |
| **Dense-20 Random 200K** | `250,93 → 189,71 → 190,07` | `211,23 → 151,00 →` **`145,88`** | `1,32×` → **`1,45×`** | 40,52% |
| **Hub 100K** | `71,68 → 67,93 → 62,47` | `74,54 → 60,81 →` **`57,04`** | `1,15×` → **`1,31×`** | 52,04% |
| **2D Grid 300×300** | `32,59 → 33,49 → 33,05` | `32,16 → 33,64 → 34,07` | `0,99×` → `0,94×` | 31,05% |

### 4.4. Kết luận và giới hạn đo
- **Hết chậm ngược ở 8T:** Trên `Sparse 200K`, 8T đã nhanh hơn rõ rệt so với 4T (`99,45 ms` vs `109,56 ms`), đảo ngược tình trạng chậm ngược trước đó (`136,24 ms` vs `133,46 ms`).
- **Giới hạn đo:** Giữa hai lượt chạy có dao động tải nền/turbo CPU khoảng 10–15% ở mức `1T`; tuy nhiên tỷ lệ nội tại `1T → 8T` và thời gian nhánh `par` trong `FindPivots` (`20,21 → 7,00 ms`) xác nhận lợi ích trực tiếp của thay đổi. Nút thắt tuần tự lớn nhất còn lại là `D.Pull` (`40,5% – 52,0%` ở 8T). Chưa chạy lại TSan với Clang+Archer cho riêng bản vá này.
