# Báo Cáo Tối Ưu Cách 1: Tái Sử Dụng Per-Level `LevelWorkspace` & `D1SkipList` Memory Pool

- **Ngày thực hiện:** 21/11/2026 (Asia/Saigon)
- **Nhánh làm việc:** `Viet` (cục bộ, sau commit `21fbf3f`)
- **Môi trường biên dịch & đo đạc:** Windows / PowerShell, GCC `16.2.0` (`-std=c++20 -O3 -march=native -mcx16 -Wall -Wextra -fopenmp`), OpenMP `5.2`, CPU `12` luồng logic.
- **Trạng thái kiểm thử:** **4/4 bộ test PASS 100%** (`test_person1`, `test_d_stress`, `verify_parallel_bmssp`, `test_parallel_edge_cases`), **96/96 tổ hợp benchmark 12 đồ thị `OK`** (khớp 100% Oracle `dijkstra_sequential`, `tol = 1e-9`).

---

## 1. Nguyên Nhân Gốc Rễ Gây Chậm Trước Khi Tối Ưu

Khi kiểm tra mã nguồn nóng ở [bmssp.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/bmssp.cpp) và [d1_skiplist.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/d1_skiplist.cpp), phát hiện hai nút thắt cấp phát bộ nhớ lặp lại ở **mọi lời gọi đệ quy `bmssp_rec`**:

1. **Tạo mới và hủy toàn bộ `ConcurrentFrontierD` + các buffer trong mỗi lần vào `bmssp_rec`:**
   - Trước đây, mỗi lời gọi đệ quy đều thực thi `std::make_unique<ConcurrentFrontierD>(ctx.states, ctx.graph.n)` (chứa `std::array<PoolShard, 64>`, `D0Deque`, `D1SkipList`, các `std::mutex` và 5 `std::vector` buffer nội bộ), đồng thời cấp phát mới `thread_k_buffers`, `miniS`, `edge_prefix`, `K`, và `complete.reserve(min(quota, n))`, rồi hủy sạch khi thoát khỏi lời gọi đó.
   - Do số lần gọi đệ quy ở các tầng thấp (`level = 1, 2`) rất lớn, chi phí cấp phát/hủy bộ nhớ trên heap chiếm phần lớn thời gian chạy — đặc biệt thấy rõ ở đồ thị nhỏ (`weighted_100`: `Concurrent` mất `1,021 ms`, chậm hơn `Mutex` `0,102 ms` tới 10×) và 2D Grid (`300×300`: `Concurrent` mất `101,89 ms`, chậm hơn `Mutex` `78,74 ms`).

2. **`D1SkipList::init()` giải phóng toàn bộ chunk của 64-shard `ThreadLocalBlockPool` rồi cấp phát lại ngay lập tức:**
   - Trong [d1_skiplist.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/d1_skiplist.cpp), hàm `init()` gọi `destroy_all_locked()`, trong đó dòng `shard.pool.clear()` giải phóng toàn bộ `chunks_` về hệ điều hành, rồi ngay dòng tiếp theo `init_sentinel_and_tail_locked()` lại cấp phát mới chunk bộ nhớ cho `head_` và `tail_`.
   - Trong khi đó, [memory_pool.hpp](file:///c:/ttss/bmssp_parallel/include/core/memory_pool.hpp#L85-L89) đã có sẵn phương thức `reset()` cho phép đặt lại con trỏ slot và free-list trong $O(1)$ **mà giữ nguyên bộ nhớ chunk đã cấp phát**.

---

## 2. Cách Xử Lý (Triển Khai Cách 1)

Nguyên lý an toàn: tại mỗi thời điểm trên ngăn xếp đệ quy của Algorithm 3, **chỉ có tối đa 1 lời gọi `bmssp_rec` đang hoạt động ở mỗi tầng `level`**, vì tầng `level` gọi tuần tự xuống `level - 1`. Do đó, tài nguyên của mỗi tầng có thể được cấp phát **1 lần duy nhất** và tái sử dụng hoàn toàn an toàn.

### Các thay đổi cụ thể trong mã nguồn (3 file)

1. **[include/data_structure/d1_skiplist.hpp](file:///c:/ttss/bmssp_parallel/include/data_structure/d1_skiplist.hpp#L162) & [src/data_structure/d1_skiplist.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/d1_skiplist.cpp#L338-L379):**
   - Thêm tham số `bool release_memory = true` cho `D1SkipList::destroy_all_locked(bool release_memory)`.
   - Destructor `~D1SkipList()` gọi `destroy_all_locked(true)` → chạy `shard.pool.clear()` để giải phóng hoàn toàn bộ nhớ khi kết thúc (0 memory leak).
   - `D1SkipList::init()` và `D1SkipList::clear()` gọi `destroy_all_locked(false)` → sau khi gọi `std::destroy_at(...)` cho mọi `BlockNode` đang sống và `retired_nodes_`, chạy `shard.pool.reset()` giữ nguyên các chunk đã cấp phát để tái sử dụng trong $O(1)$.

2. **[src/algorithm/bmssp.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/bmssp.cpp#L31-L82):**
   - Tạo `struct LevelWorkspace` chứa `std::unique_ptr<IFrontierD> frontier`, `thread_k_buffers`, `miniS`, `edge_prefix`, và `K`.
   - Khởi tạo sẵn `std::vector<LevelWorkspace> level_workspaces(max_lvl + 1)` trong `BMSSPContext` (1 lần duy nhất cho `lvl = 1..max_level` ở đầu `run_bmssp_direct`).
   - Trong `bmssp_rec(ctx, level, B, S)`: lấy trực tiếp `ctx.level_workspaces[level]`, gọi `D->init(batch_size, B)` và `.clear()` các buffer (giữ nguyên `capacity`), đồng thời giới hạn `complete.reserve(...)` hợp lý theo `min(quota, min(n, max(S.size() * k, 64)))` để tránh cấp phát thừa ở tầng trung gian.

---

## 3. Kết Quả Chạy Thực Tế Trên 12 Đồ Thị (`100` → `200.000` Đỉnh)

### 3.1. Bảng Thời Gian Trung Vị (`Median ms`) Sau Khi Áp Dụng Cách 1

*(Chạy bằng [bench_parallel_suite.cpp](file:///c:/ttss/bmssp_parallel/benchmarks/bench_parallel_suite.cpp); `n <= 1K`: 5 warmup + 21 mẫu; `1K < n <= 50K`: 2 warmup + 7 mẫu; `n > 50K`: 1 warmup + 5 mẫu; 96/96 tổ hợp khớp Oracle `dijkstra_sequential` với `Status = OK`.)*

| # | Đồ thị | Đỉnh ($n$) | Cạnh ($m$) | `Dijkstra` (1T) | `bmssp_seq` (1T) | `Mutex` 1T | `Mutex` 4T | `Mutex` 8T | `Conc` 1T | `Conc` 4T | `Conc` 8T | Tự tăng tốc `Conc` (1T/8T) | A/B `M8T/C8T` | `C8T/Dijk` | Status |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|:---:|
| 1 | **`weighted_100`** *(dự án)* | **100** | 240 | **0,005** | 0,219 | 0,063 | 0,063 | 0,064 | 0,424 | 0,406 | **0,411** | 1,03× | 0,16× | 82,2× | OK |
| 2 | **`Sparse Random 100`** | **100** | 500 | **0,004** | 0,270 | 0,073 | 0,086 | 0,075 | 0,421 | 0,418 | **0,422** | 1,00× | 0,18× | 105,5× | OK |
| 3 | **`weighted_1000`** *(dự án)* | **1.000** | 2.400 | **0,028** | 1,139 | 0,320 | 0,493 | 0,419 | 0,967 | 0,974 | **0,979** | 0,99× | 0,43× | 35,0× | OK |
| 4 | **`Sparse Random 10K`** | **10.000** | 50.000 | **2,724** | 24,988 | 9,772 | 15,835 | 17,137 | 10,141 | 12,447 | **12,725** | 0,80× | 1,35× | 4,67× | OK |
| 5 | **`Sparse Random 20K`** | **20.000** | 100.000 | **6,629** | 48,510 | 24,086 | 40,668 | 42,941 | 19,443 | 23,858 | **24,268** | 0,80× | 1,77× | 3,66× | OK |
| 6 | **`2D Grid 150×150`** | **22.500** | 89.400 | **3,691** | 44,769 | 16,306 | 18,265 | 18,402 | 16,970 | 19,540 | **18,887** | 0,90× | 0,97× | 5,12× | OK |
| 7 | **`Hub 20K`** (`deg0=10K`) | **20.000** | 90.000 | **6,345** | 89,293 | 25,127 | 38,458 | 40,535 | 23,267 | 27,168 | **28,173** | 0,83× | 1,44× | 4,44× | OK |
| 8 | **`Sparse Random 50K`** | **50.000** | 250.000 | **21,384** | 145,560 | 70,001 | 98,897 | 99,708 | 55,901 | 56,285 | **55,293** | 1,01× | 1,80× | 2,59× | OK |
| 9 | **`2D Grid 300×300`** | **90.000** | 358.800 | **18,077** | 176,319 | 70,574 | 72,175 | 70,300 | 66,889 | 67,643 | **67,620** | 0,99× | **1,04×** | 3,74× | OK |
| 10 | **`Sparse Random 100K`** | **100.000** | 500.000 | **49,228** | 297,318 | 194,904 | 253,642 | 303,609 | 109,448 | 113,427 | **108,464** | 1,01× | **2,80×** | 2,20× | OK |
| 11 | **`Hub 100K`** (`deg0=50K`) | **100.000** | 450.000 | **47,214** | 593,571 | 179,188 | 220,563 | 242,988 | 149,534 | 125,577 | **119,041** | **1,26×** | **2,04×** | 2,52× | OK |
| 12 | **`Sparse Random 200K`** | **200.000** | 1.000.000 | **131,946** | 619,534 | 465,569 | 723,844 | 662,434 | 228,238 | 221,339 | **197,467** | **1,16×** | **3,35×** | **1,50×** | OK |

### 3.2. So Sánh Trực Tiếp Trước vs Sau Cách 1 (`ConcurrentFrontierD`, 8 luồng)

| # | Đồ thị | Trước Cách 1 (`Conc 8T`) | Sau Cách 1 (`Conc 8T`) | Mức nhanh hơn | Giảm thời gian | Tỷ số `C8T/Dijk` (Trước → Sau) |
|---|---|---:|---:|---:|---:|---|
| 1 | `weighted_100` (`n=100`) | 1,021 ms | **0,411 ms** | **2,48×** | **-59,7%** | `255,3×` → **`82,2×`** |
| 2 | `Sparse Random 100` (`n=100`) | 1,127 ms | **0,422 ms** | **2,67×** | **-62,6%** | `161,0×` → **`105,5×`** |
| 3 | `weighted_1000` (`n=1.000`) | 2,399 ms | **0,979 ms** | **2,45×** | **-59,2%** | `85,7×` → **`35,0×`** |
| 4 | `Sparse Random 10K` | 18,415 ms | **12,725 ms** | **1,45×** | **-30,9%** | `5,78×` → **`4,67×`** |
| 5 | `Sparse Random 20K` | 30,807 ms | **24,268 ms** | **1,27×** | **-21,2%** | `4,46×` → **`3,66×`** |
| 6 | `2D Grid 150×150` (`n=22,5K`) | 32,985 ms | **18,887 ms** | **1,75×** | **-42,7%** | `7,76×` → **`5,12×`** |
| 7 | `Hub 20K` (`deg0=10K`) | 32,819 ms | **28,173 ms** | **1,16×** | **-14,2%** | `4,93×` → **`4,44×`** |
| 8 | `Sparse Random 50K` | 63,997 ms | **55,293 ms** | **1,16×** | **-13,6%** | `2,60×` → **`2,59×`** |
| 9 | `2D Grid 300×300` (`n=90K`) | 101,885 ms | **67,620 ms** | **1,51×** | **-33,6%** | `4,93×` → **`3,74×`** |
| 10 | `Sparse Random 100K` | 126,564 ms | **108,464 ms** | **1,17×** | **-14,3%** | `1,92×` → `2,20×` *(Dijk nền dao động)* |
| 11 | `Hub 100K` (`deg0=50K`) | 139,279 ms | **119,041 ms** | **1,17×** | **-14,5%** | `2,48×` → `2,52×` |
| 12 | `Sparse Random 200K` (`m=1M`) | 242,696 ms | **197,467 ms** | **1,23×** | **-18,6%** | `1,86×` → **`1,50×`** |

---

## 4. Nhận Xét & Giới Hạn Minh Bạch

1. **Hiệu quả rõ rệt trên toàn bộ 12 đồ thị:** Cách 1 giảm **14% – 63%** thời gian chạy của `ConcurrentFrontierD` trên mọi kích thước:
   - Đồ thị nhỏ (`100` – `1.000` đỉnh): nhanh hơn **2,45× – 2,67×**; ở `weighted_1000` (`0,979 ms`) lần đầu vượt `bmssp_seq` (`1,139 ms`).
   - 2D Grid (`150×150`, `300×300`): nhanh hơn **1,51× – 1,75×**; ở `2D Grid 300×300`, `Concurrent 8T` (`67,62 ms`) **lần đầu vượt `Mutex 8T` (`70,30 ms`)**.
   - `Sparse 200K` (1.000.000 cạnh): giảm từ `242,70 ms` xuống **`197,47 ms`** (nhanh hơn `bmssp_seq` **`3,14×`**, nhanh hơn `Mutex 8T` **`3,35×`**, và chỉ còn chậm hơn `Dijkstra 1T` **`1,50×`**).
2. **Lưu ý về nhiễu nền giữa hai phiên đo:** Thời gian đo ở hai thời điểm khác nhau có dao động nền hệ điều hành nhẹ (ví dụ `Dijkstra` trên `100K` là `65,8 ms` ở phiên trước vs `49,2 ms` ở phiên này; còn trên `200K` gần như giữ nguyên `130,4 ms` vs `131,9 ms`). Do đó cột `C8T/Dijk` được đặt song song để đánh giá khách quan.
3. **Chưa đụng tới Cách 2, 3, 4:** Trong phạm vi yêu cầu này chỉ áp dụng đúng Cách 1; các tối ưu tiếp theo (Hybrid Frontier cho tầng đáy, thay `.load().dist` bằng `.load_dist()` trong `d0`/`d1`, và batching `D->insert`) vẫn còn dư địa nếu cần tiếp tục tăng tốc.
