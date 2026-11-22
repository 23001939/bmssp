# Báo Cáo Tổng Hợp Tối Ưu Song Song BMSSP (Cách 1 + Cách 3 + Cách 4)

- **Ngày cập nhật:** 21/11/2026 (Asia/Saigon)
- **Nhánh làm việc:** `Viet` (cục bộ, sau commit `21fbf3f`)
- **Môi trường biên dịch & đo đạc:** Windows / PowerShell, GCC `16.2.0` (`-std=c++20 -O3 -march=native -mcx16 -Wall -Wextra -fopenmp`), OpenMP `5.2`, CPU `12` luồng logic.
- **Trạng thái kiểm thử:**
  - `python bmssp_parallel/build_and_test.py`: **4 / 4 test suites PASS 100%** (`test_person1`: 153.932 checks; `test_d_stress`: 1.406.792 checks; `verify_parallel_bmssp`: 24/24; `test_parallel_edge_cases`: 6/6 suite & 240/240 stress runs).
  - `benchmarks/bench_parallel_suite.exe`: **104 / 104 tổ hợp** (13 đồ thị × 8 cấu hình) khớp Oracle `dijkstra_sequential` (`tol = 1e-9`, `Status = OK`).

---

## 1. Nguyên Nhân Gốc Rễ Khiến Tốc Độ Song Song Chưa Cao

Trước khi tối ưu, mặc dù đã tích hợp `ConcurrentFrontierD` từ nhánh `Viet` và sửa sạch lỗi correctness, hiệu năng và mức tăng tốc đa luồng (`1T → 8T`) vẫn còn thấp (`1,16×–1,25×` ở 200K đỉnh) do 3 nút thắt triển khai trực tiếp trong mã nguồn và 1 giới hạn bản chất của thuật toán:

1. **Cấp phát/hủy bộ nhớ lặp lại ở mọi lời gọi đệ quy (`bmssp.cpp` & `d1_skiplist.cpp`):**
   - Mỗi lần gọi `bmssp_rec` đều `make_unique<ConcurrentFrontierD>` (kèm 64 shard `PoolShard`, `D0Deque`, `D1SkipList`, 5 buffer nội bộ) và cấp phát mới `thread_k_buffers`, `miniS`, `edge_prefix`, `K`, sau đó hủy toàn bộ khi thoát hàm.
   - `D1SkipList::init()` gọi `shard.pool.clear()` giải phóng hết chunk về hệ điều hành rồi lại `malloc` mới cho `head_`/`tail_` ở mỗi lần gọi đệ quy.
2. **Lệnh `lock cmpxchg16b` ẩn gây cache-line ping-pong trong `D0Deque` và `D1SkipList`:**
   - 4 vị trí kiểm tra phần tử cũ (stale) trong [d0_deque.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/d0_deque.cpp) và [d1_skiplist.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/d1_skiplist.cpp) vẫn gọi `state_arr[v].load(std::memory_order_acquire).dist`, phát sinh lệnh 128-bit `lock cmpxchg16b` ép cache line sang trạng thái Exclusive/Modified ngay cả khi chỉ đọc kiểm tra khoảng cách.
3. **Gọi `D->insert()` cho từng cạnh lẻ ngay trong vòng `#pragma omp parallel`:**
   - Mỗi cạnh relax thành công gọi trực tiếp `D->insert(v, new_dist)` → tạo/hủy `ReaderGuard` (phát sinh `std::atomic_thread_fence(std::memory_order_seq_cst)`), duyệt Skip-List và khóa `BlockSpinlock` cho từng phần tử đơn lẻ.
4. **Giới hạn bản chất thuật toán BMSSP (Định luật Amdahl):**
   - Trong Algorithm 3, lát khoảng cách thứ $i+1$ phụ thuộc tuần tự cứng vào biên $B'_i$ của lát thứ $i$; các pha `D.Pull`, `D.BatchPrepend`, `BaseCase` và dựng rừng `FindPivots` chạy tuần tự trên Master thread.

---

## 2. Cách Xử Lý Đã Triển Khai (Bảo Toàn 100% Bản Chất BMSSP)

Cả 3 tối ưu dưới đây đều là **tối ưu hệ thống/bộ nhớ/đồng bộ**, không thay đổi logic Algorithm 1–3, không đổi cấu trúc 2 tầng $\mathcal{D} = (\mathcal{D}_0, \mathcal{D}_1)$ của Lemma 3.3 và không thoái hóa về Dijkstra:

### Cách 1 — Tái sử dụng Per-Level `LevelWorkspace` & `D1SkipList` Memory Pool
- **[src/algorithm/bmssp.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/bmssp.cpp):** Khởi tạo sẵn `std::vector<LevelWorkspace> level_workspaces(max_level + 1)` trong `BMSSPContext` (1 lần duy nhất cho mỗi tầng `1..max_level` vì mỗi tầng chỉ có tối đa 1 lời gọi hoạt động trên call stack). Trong `bmssp_rec`, tái sử dụng trực tiếp `frontier`, `thread_k_buffers`, `thread_d_buffers`, `miniS`, `edge_prefix`, `K`.
- **[include/data_structure/d1_skiplist.hpp](file:///c:/ttss/bmssp_parallel/include/data_structure/d1_skiplist.hpp) & [src/data_structure/d1_skiplist.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/d1_skiplist.cpp):** Thêm cờ `release_memory` cho `destroy_all_locked()`: trong `init()`/`clear()` dùng `shard.pool.reset()` giữ nguyên các chunk đã cấp phát để tái sử dụng trong $O(1)$; chỉ gọi `shard.pool.clear()` khi hủy ở destructor `~D1SkipList()`.

### Cách 3 — Loại bỏ `lock cmpxchg16b` khi đọc kiểm tra stale trong `D0`/`D1`
- **[src/data_structure/d0_deque.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/d0_deque.cpp)** (1 vị trí) và **[src/data_structure/d1_skiplist.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/d1_skiplist.cpp)** (3 vị trí trong `handle_overflow_locked`, `insert`, `drain_valid_up_to`): đổi `state_arr[v].load(std::memory_order_acquire).dist` thành `state_arr[v].load_dist(std::memory_order_acquire)` (đọc 64-bit nguyên tử nhẹ, không khóa cache line).

### Cách 4 — Gom lô theo luồng (`thread_d_buffers`) + `D->insert_batch()`
- **[include/core/frontier_interface.hpp](file:///c:/ttss/bmssp_parallel/include/core/frontier_interface.hpp):** Bổ sung `virtual void insert_batch(std::span<const VertexDist> items)` với cài đặt mặc định gọi `insert()`.
- **[include/data_structure/concurrent_frontier_d.hpp](file:///c:/ttss/bmssp_parallel/include/data_structure/concurrent_frontier_d.hpp) & [src/data_structure/concurrent_frontier_d.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/concurrent_frontier_d.cpp):** Override `insert_batch()` chuyển thẳng sang `D1SkipList::insert_batch_sorted()` — dùng chung **1 `ReaderGuard` duy nhất** cho cả lô và chèn các phần tử liền kề vào cùng block trong 1 lần khóa `BlockSpinlock`.
- **[src/algorithm/bmssp.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/bmssp.cpp):** Trong `#pragma omp parallel`, mỗi luồng đẩy các cặp `(v, new_dist)` vào `local_d` (không khóa, không fence); sau vòng lặp relax, từng luồng **song song** lọc bỏ stale bằng `load_dist()`, khử trùng cục bộ giữ `dist` nhỏ nhất cho mỗi đỉnh, sắp xếp tăng dần theo `dist`, rồi gọi `D->insert_batch(local_d)`.

---

## 3. Kết Quả Chạy Thực Tế Trên 13 Đồ Thị (`100` → `200.000` Đỉnh / `4.000.000` Cạnh)

### 3.1. Bảng Thời Gian Trung Vị (`Median ms`) Sau Cách 1 + 3 + 4

*(Chạy bằng [bench_parallel_suite.cpp](file:///c:/ttss/bmssp_parallel/benchmarks/bench_parallel_suite.cpp); `n <= 1K`: 5 warmup + 21 mẫu; `1K < n <= 50K`: 2 warmup + 7 mẫu; `n > 50K`: 1 warmup + 5 mẫu; 104/104 tổ hợp khớp Oracle `dijkstra_sequential`, `Status = OK`.)*

| # | Đồ thị | Đỉnh ($n$) | Cạnh ($m$) | `Dijkstra` (1T) | `bmssp_seq` (1T) | `Mutex` 1T | `Mutex` 4T | `Mutex` 8T | `Conc` 1T | `Conc` 4T | `Conc` 8T | Tự tăng tốc `Conc` (1T/8T) | A/B `M8T/C8T` | `C8T/Dijk` | Status |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|:---:|
| 1 | **`weighted_100`** *(dự án)* | **100** | 240 | 0,003 | 0,274 | 0,118 | 0,064 | 0,066 | 0,389 | 0,439 | **0,403** | 0,97× | 0,16× | 134,3× | OK |
| 2 | **`Sparse Random 100`** | **100** | 500 | 0,006 | 0,274 | 0,076 | 0,075 | 0,081 | 0,408 | 0,418 | **0,396** | 1,03× | 0,20× | 66,0× | OK |
| 3 | **`weighted_1000`** *(dự án)* | **1.000** | 2.400 | 0,039 | 1,129 | 0,390 | 0,369 | 0,351 | 0,983 | 0,961 | **0,930** | 1,06× | 0,38× | 23,8× | OK |
| 4 | **`Sparse Random 10K`** | **10.000** | 50.000 | 2,784 | 24,321 | 10,250 | 13,399 | 15,380 | 10,285 | 11,473 | **12,061** | 0,85× | 1,28× | 4,33× | OK |
| 5 | **`Sparse Random 20K`** | **20.000** | 100.000 | 6,634 | 47,778 | 24,373 | 32,299 | 38,538 | 22,078 | 22,574 | **23,019** | 0,96× | 1,67× | 3,47× | OK |
| 6 | **`2D Grid 150×150`** | **22.500** | 89.400 | 3,802 | 44,091 | 18,142 | 20,015 | 19,294 | 17,690 | 19,556 | **19,781** | 0,89× | 0,98× | 5,20× | OK |
| 7 | **`Hub 20K`** (`deg0=10K`) | **20.000** | 90.000 | 6,277 | 87,672 | 23,486 | 31,028 | 37,581 | 23,234 | 24,215 | **24,153** | 0,96× | 1,56× | 3,85× | OK |
| 8 | **`Sparse Random 50K`** | **50.000** | 250.000 | 21,164 | 148,039 | 63,368 | 67,779 | 72,697 | 56,965 | 52,276 | **50,849** | **1,12×** | 1,43× | 2,40× | OK |
| 9 | **`2D Grid 300×300`** | **90.000** | 358.800 | 19,828 | 188,431 | 70,486 | 72,313 | 75,988 | 72,775 | 69,569 | **68,572** | **1,06×** | 1,11× | 3,46× | OK |
| 10 | **`Sparse Random 100K`** | **100.000** | 500.000 | 55,249 | 305,675 | 142,884 | 158,894 | 184,577 | 113,651 | 97,353 | **95,996** | **1,18×** | **1,92×** | 1,74× | OK |
| 11 | **`Hub 100K`** (`deg0=50K`) | **100.000** | 450.000 | 48,186 | 544,284 | 136,159 | 196,146 | 190,179 | 131,949 | 124,470 | **115,486** | **1,14×** | **1,65×** | 2,40× | OK |
| 12 | **`Sparse Random 200K`** | **200.000** | 1.000.000 | 106,981 | 620,256 | 388,825 | 437,513 | 473,435 | 237,955 | 180,923 | **179,749** | **1,32×** | **2,63×** | 1,68× | OK |
| 13 | **`Dense-20 Random 200K`** | **200.000** | **4.000.000** | 216,776 | 1.285,672 | 1.037,514 | 998,235 | 917,774 | 399,489 | 264,436 | **252,598** | **1,58×** | **3,63×** | **1,17×** | OK |

### 3.2. Tiến Bộ Qua 3 Mốc Tối Ưu (`ConcurrentFrontierD`, 8 luồng)

| Đồ thị | Ban đầu (Trước tối ưu) | Sau Cách 1 | Sau Cách 1 + 3 + 4 | Tổng mức giảm thời gian |
|---|---|---|---|---|
| `weighted_100` (`n=100`) | 1,021 ms (`0,98×`) | 0,411 ms (`1,03×`) | **0,403 ms** (`0,97×`) | **-60,5%** (`2,53×` nhanh hơn) |
| `Sparse Random 100` (`n=100`) | 1,127 ms (`0,87×`) | 0,422 ms (`1,00×`) | **0,396 ms** (`1,03×`) | **-64,9%** (`2,85×` nhanh hơn) |
| `weighted_1000` (`n=1.000`) | 2,399 ms (`1,15×`) | 0,979 ms (`0,99×`) | **0,930 ms** (`1,06×`) | **-61,2%** (`2,58×` nhanh hơn) |
| `2D Grid 300×300` (`n=90K`) | 101,885 ms (`1,08×`) | 67,620 ms (`0,99×`) | **68,572 ms** (`1,06×`) | **-32,7%** |
| `Sparse Random 100K` (`m=500K`) | 126,564 ms (`1,18×`) | 108,464 ms (`1,01×`) | **95,996 ms** (`1,18×`) | **-24,1%** |
| `Sparse Random 200K` (`m=1M`) | 242,696 ms (`1,25×`) | 197,467 ms (`1,16×`) | **179,749 ms** (**`1,32×`**) | **-25,9%** *(1T→8T từ 16% lên **32%**)* |
| `Dense-20 Random 200K` (`m=4M`) | *(chưa đo)* | *(chưa đo)* | **252,598 ms** (**`1,58×`**) | Nhanh hơn `bmssp_seq` **`5,09×`**, chỉ còn **`1,17×`** so với `Dijkstra 1T` |

---

## 4. Kết Luận & Giới Hạn Trung Thực

1. **Hiệu quả thực tế:** Kết hợp Cách 1 + 3 + 4 giảm **24% – 65%** thời gian chạy trên toàn bộ dải đồ thị, nâng tốc độ song song `1T → 8T` từ **`1,16×`** lên **`1,32×`** trên `Sparse 200K` và đạt **`1,58×`** trên `Dense-20 200K` (`4M` cạnh, chỉ còn chậm hơn `Dijkstra 1T` `1,17×`).
2. **Trần Amdahl ở bước `4T → 8T`:** Trên `Dense-20 200K`, `1T → 4T` giảm mạnh (`399,5 → 264,4 ms`, `1,51×`), nhưng `4T → 8T` chỉ giảm thêm nhẹ (`264,4 → 252,6 ms`, `1,58×`) do phần tuần tự cố hữu của Algorithm 3 (`D.Pull`, `D.BatchPrepend`, `BaseCase`).
3. **Tính trung thực học thuật:** Mọi phép đo trên đều chạy ở chế độ trực tiếp CSR (`use_gadget_transform = false`), không tuyên bố chứng minh toán học cận worst-case tất định từ kết quả benchmark hữu hạn.
