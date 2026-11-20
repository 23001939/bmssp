# Báo Cáo Cải Thiện & Kiểm Chứng Parallel BMSSP (`fix/parallel-bmssp-remediation`)

- **Ngày hoàn thành:** 20/11/2026 (Asia/Saigon)
- **Nhánh gốc đối chiếu (Trước):** `parallel-bmssp` (commit `557fc8084b1da8814d3b4e9de458cfe3914b350c`)
- **Nhánh cấu trúc dữ liệu hợp nhất:** `Viet` (commit `b0148bfe563a95b5bbf1873563e939a4fdb7defb` — Người 2: `D0Deque`, `D1SkipList`, `ConcurrentFrontierD`)
- **Nhánh triển khai (Sau):** `fix/parallel-bmssp-remediation`
- **Môi trường biên dịch & đo đạc:** Windows / PowerShell, GCC `16.2.0` (`w64devkit`), flags `-std=c++20 -O3 -march=native -mcx16 -Wall -Wextra -fopenmp`, OpenMP `5.2`, CMake + MinGW Makefiles + CTest, CPU 12 luồng logic (`std::thread::hardware_concurrency() == 12`).
- **Tổ chức thực thi:** Main Agent điều phối **4 subagent (`Model: pro`)** làm việc song song theo ma trận sở hữu file độc quyền (RACI), sau đó Main Agent tổng hợp, biên dịch và xác minh toàn bộ đầu ra.

---

## 1. Tóm Tắt Đối Chiếu Trước / Sau

| Hạng mục | Trước (`parallel-bmssp`, `557fc80`) | Sau (`fix/parallel-bmssp-remediation`) | Bằng chứng xác minh |
|---|---|---|---|
| **Chu trình trọng số 0 (`0 ↔ 1`, w=0)** | **TREO VÔ HẠN** (bị kill sau 3 giây) | **PASS** tức thì, `MaxDiff = 0`, cây `pred` hợp lệ | `test_parallel_edge_cases.exe` (Suite 1a–1d) |
| **Chế độ Gadget Transform (`use_gadget_transform = true`)** | **TREO VÔ HẠN** ngay trên đồ thị 12 đỉnh (do gadget chèn chu trình 0-weight) và ánh xạ sai `pred[i] == i` | **PASS** trên đồ thị bậc cao 60 đỉnh, random 300 đỉnh và đồ thị có chu trình 0; `pred[i] != i`, không chu trình | `test_parallel_edge_cases.exe` (Suite 5a–5c) |
| **Data race `root[v]` trong `FindPivots`** | `std::vector<uint32_t> root(n)` ghi song song không đồng bộ trong `#pragma omp for`, cấp phát $O(n)$ mỗi lần gọi | Khử 100% data race: dựng rừng & tính `subtree_size` tuần tự sau barrier trên tập `vis` nhỏ; tái sử dụng `FindPivotsWorkspace` | `find_pivots.cpp` + 240/240 stress runs |
| **`EpochDeduplicator` sai phạm vi** | Truyền `step = 1..k` cục bộ vào đối tượng dùng chung toàn cục → đỉnh bị khóa vĩnh viễn sau lời gọi đầu | Thêm `allocate_epoch()` đơn điệu toàn cục (tự reset an toàn khi tràn `uint32_t`) + tách `vis_tracker` | `primitives.hpp`, `find_pivots.cpp` |
| **Truyền `completed` giữa các tầng đệ quy** | `level == 0` set `is_completed[u] = true` khiến tầng cha bỏ qua toàn bộ `nw_complete` | Tách `global_completed` (`std::vector<uint8_t>`) khỏi `local_complete_dedups` riêng từng tầng `0..max_level` | `bmssp.cpp` |
| **Ngưỡng tách `complete_B` vs `trying_B` & fallback lỗi** | Không kiểm tra cận dưới `complete_B`; nhánh `nw_complete.empty()` đánh dấu xong mà bỏ qua relax | Áp chặt miền `[trying_B, B)` → `D->insert`, `[complete_B, trying_B)` → `K`; xóa nhánh fallback bỏ qua relax | `bmssp.cpp` |
| **Lặp/trùng đỉnh trong `BaseCase`** | Không có `settled`, đỉnh có cạnh 0 bị pop nhiều lần làm tràn `k + 1` và sai `new_bound` | Bổ sung tập `settled` cục bộ, mỗi đỉnh hoàn tất tối đa 1 lần; xử lý an toàn ca hòa `last_d == d_src` | `base_case.cpp` |
| **Tích hợp `ConcurrentFrontierD`** | Hardcode `MutexFrontierD`; file `concurrent_frontier_d.hpp` cũ có lỗi use-after-free, `push_back` sai ngữ nghĩa và thiếu Lazy Deletion | Thay bằng bản chuẩn từ nhánh `Viet` (`D0Deque` + `D1SkipList` B-Link + Eager Cleanup + EBR), gắn `set_state_array`, giữ `MutexFrontierD` để đối chứng A/B | `test_d_stress.exe` (1.406.792 checks) + `bench_parallel_bmssp.exe` |
| **Tham số `(k, t)` & UB dịch bit** | Cố định `k = 1, t = 1`; `1ULL << (level * t)` có nguy cơ UB nếu `>= 64` | Tính `k = floor(log2(n)^(1/3))`, `t = floor(log2(n)^(2/3))` ($\ge 1$); `safe_pow2_capped(shift, cap)` | `bmssp.cpp` |
| **Cache-line bouncing ở `AtomicVertexState::load`** | Mọi phép đọc `dist` đều phát `lock cmpxchg16b` ép cache-line sang Exclusive/Modified | Bổ sung `load_dist()` 64-bit atomic load nhẹ + fast-path trong `relax_edge_atomic` | `types.hpp` |
| **Phân phối việc trên đồ thị hub lệch bậc** | Chia vòng ngoài theo `active.size()` (1 hub 20K cạnh chỉ chạy 1 luồng) | Chia đều theo cạnh (edge-level partitioning) dùng prefix-sum bậc cạnh + `std::upper_bound` | `find_pivots.cpp`, `bmssp.cpp`, Suite 4 |
| **Hệ thống Build, CTest & Test Assertions** | `CMakeLists.txt` thiếu target thuật toán/test; `test_person1` dùng `assert(false)` bị tắt dưới `-DNDEBUG` | Đăng ký đủ 3 lib + 6 exe + `CTest` 4/4 tests; thay `assert(false)` bằng `std::exit(1)` | `CMakeLists.txt`, `build_and_test.py`, `ctest` 4/4 PASS |

---

## 2. Chi Tiết Thay Đổi Theo Từng Subagent

### SA-1 — Core & Atomics Engineer
- **[include/core/types.hpp](file:///c:/ttss/bmssp_parallel/include/core/types.hpp):**
  - Bổ sung `AtomicVertexState::load_dist(std::memory_order)` sử dụng `__atomic_load_n` 64-bit trên trường `dist` (kèm `static_assert(offsetof(VertexState, dist) == 0)` và `sizeof(double) == 8`) để đọc khoảng cách nguyên tử mà không phát lệnh `lock cmpxchg16b`.
  - Thêm fast-path `if (new_dist >= target.load_dist()) return false;` trong `relax_edge_atomic` trước khi gọi 128-bit CAS.
  - Giữ nguyên toàn bộ API cũ để bảo đảm tương thích ngược 100% với Người 1 và Người 2.
- **[include/core/primitives.hpp](file:///c:/ttss/bmssp_parallel/include/core/primitives.hpp):**
  - Nâng cấp `EpochDeduplicator`: thêm `allocate_epoch()` tăng đơn điệu toàn cục (tự reset an toàn khi chạm `UINT32_MAX`), `is_claimed(v, target_epoch)`, và `size()`.

### SA-2 — `FindPivots` & `BaseCase` Engineer
- **[include/algorithm/find_pivots.hpp](file:///c:/ttss/bmssp_parallel/include/algorithm/find_pivots.hpp) & [src/algorithm/find_pivots.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/find_pivots.cpp):**
  - Thêm `FindPivotsWorkspace` để tái sử dụng bộ nhớ xuyên suốt các lời gọi đệ quy, loại bỏ cấp phát $O(n)$ lặp lại.
  - Dùng `deduplicator.allocate_epoch()` cho mỗi bước `step = 1..k` và `ws.vis_tracker` để khử trùng lặp đỉnh trong `vis` (và khử trùng `S`).
  - Chuẩn hóa relax `<=` theo Algorithm 1 (cho phép mở rộng rừng `F` qua cạnh hòa khoảng cách).
  - Triển khai **edge-level work partitioning** qua prefix-sum bậc cạnh + `std::upper_bound` trong `#pragma omp parallel`.
  - Khử 100% data race `root[v]`: không ghi `root_of` trong vòng song song; dựng rừng & đếm `subtree_cnt` tuần tự sau barrier từ `states[v].load().pred` trên tập nhỏ `vis`, có chống chu trình trọng số 0.
- **[include/algorithm/base_case.hpp](file:///c:/ttss/bmssp_parallel/include/algorithm/base_case.hpp) & [src/algorithm/base_case.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/base_case.cpp):**
  - Bổ sung tập `settled` cục bộ (`std::unordered_set<uint32_t>`) bảo đảm mỗi đỉnh chỉ được hoàn tất tối đa 1 lần.
  - Xử lý nhất quán cạnh hòa `new_dist == curr_v` (chỉ đẩy vào heap khi `!settled.count(v)`) và trường hợp `last_d <= d_src` trên đồ thị toàn cạnh 0 để luôn bảo đảm tiến triển.

### SA-3 — `BMSSP` Recursion, Gadget & Build Engineer
- **[include/algorithm/bmssp.hpp](file:///c:/ttss/bmssp_parallel/include/algorithm/bmssp.hpp) & [src/algorithm/bmssp.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/bmssp.cpp):**
  - Thêm `enum class FrontierType : uint8_t { Concurrent = 0, Mutex = 1 };` vào chữ ký `bmssp_parallel` (mặc định `Concurrent`, tương thích ngược 100%).
  - Tách `global_completed` (`std::vector<uint8_t>`) khỏi `local_complete_dedups` (1 `EpochDeduplicator` riêng cho từng mức `0..max_level`), khắc phục dứt điểm lỗi tầng con che mất đỉnh của tầng cha.
  - Áp chặt quy tắc Algorithm 3: `[trying_B, B)` → `D->insert`, `[complete_B, trying_B)` → `K`; chặn tái enqueue đỉnh đã `global_completed` khi `new_dist == curr_v`; xóa nhánh fallback `nw_complete.empty()`; thăng cấp an toàn khi hòa tại `trying_B`.
  - Áp dụng edge-level partitioning cho pha relax `nw_complete`.
  - Tính `(k, t)` theo công thức $\log^{1/3} n, \log^{2/3} n$, dùng `safe_pow2_capped` chống UB dịch bit và không cắt sớm tầng gốc `level == max_level`.
  - Viết `reconstruct_valid_predecessors()` cho `use_gadget_transform = true`: duyệt theo mức `dist` tăng dần kết hợp BFS nội tầng (Pass 1 khớp exact IEEE-754, Pass 2 fallback dung sai `1e-12`), bảo đảm cây đường đi ngắn nhất hợp lệ 100% trên đồ thị gốc (`pred[source] == NO_PRED`, không tự trỏ, không chu trình).
- **[CMakeLists.txt](file:///c:/ttss/bmssp_parallel/CMakeLists.txt) & [build_and_test.py](file:///c:/ttss/bmssp_parallel/build_and_test.py):**
  - Hợp nhất 3 thư viện tĩnh (`bmssp_core`, `bmssp_data_structure`, `bmssp_parallel_lib`), 6 file thực thi và 4 test đăng ký `CTest`.

### SA-4 — QA, Edge-Case Tests & Benchmark Engineer
- **[tests/test_person1_comprehensive.cpp](file:///c:/ttss/bmssp_parallel/tests/test_person1_comprehensive.cpp):** Đổi `assert(false)` thành `std::exit(1)` để giữ nguyên hiệu lực khi bật `-DNDEBUG`.
- **[tests/verify_parallel_bmssp.cpp](file:///c:/ttss/bmssp_parallel/tests/verify_parallel_bmssp.cpp):** Nâng cấp kiểm tra cả `Concurrent` và `Mutex` kèm xác minh toàn bộ cây `predecessor` (`verify_predecessor_tree`).
- **[tests/test_parallel_edge_cases.cpp](file:///c:/ttss/bmssp_parallel/tests/test_parallel_edge_cases.cpp):** Bổ sung 6 bộ kiểm thử biên & stress (chu trình 0-weight, near-tie `4e-11`, đồ thị không liên thông/cô lập, hub 20.000 cạnh ra, gadget mode, và 240 lượt concurrency stress).
- **[benchmarks/bench_parallel_bmssp.cpp](file:///c:/ttss/bmssp_parallel/benchmarks/bench_parallel_bmssp.cpp):** Benchmark A/B tuần tự (2 warmup + 7 mẫu trung vị) trên Sparse 20K, 2D Grid 22.5K và Hub 20K kèm xác minh Oracle.

---

## 3. Kết Quả Kiểm Thử Thực Tế (100% PASS)

### 3.1. Chạy qua `python bmssp_parallel/build_and_test.py` (flags `-O3 -march=native -mcx16 -Wall -Wextra -fopenmp`)

| Bộ test | Kết quả | Chi tiết xác minh |
|---|---|---|
| `test_person1.exe` | **PASS** | **153.932 / 153.932 checks** (128-bit CAS, Gadget transform, MemoryPool, Primitives, Sequential BMSSP) |
| `test_d_stress.exe` | **PASS** | **1.406.792 / 1.406.792 checks** trong `1.330,76 ms` (Suite 3 giảm **53,81%** `SPLIT`; Suite 4 chạy **32 luồng / 10.000.000 ops** đạt `8,25 Mops/s`, 0 deadlock, 0 crash) |
| `verify_parallel_bmssp.exe` | **PASS** | **24 / 24 ca** (Test 1–5 × `{Concurrent, Mutex}` × đa luồng), `Max diff: 0`, `pred tree OK` |
| `test_parallel_edge_cases.exe` | **PASS** | **6 / 6 suite PASS 100%**: chu trình 0-weight (2 đỉnh, 5 đỉnh, self-loop, toàn 0 50 đỉnh), near-tie `4e-11` (`pred[1] == 2`), cô lập/`source != 0`, hub 20.000 cạnh ra, `use_gadget_transform = true` (60/300 đỉnh & 0-cycle), và **240 / 240** lượt stress đa luồng khớp cả `dist` lẫn cây `pred` |

### 3.2. Xác minh độc lập qua `CMake + MinGW Makefiles + CTest`

```text
1/4 Test #1: test_person1 .....................   Passed    0.23 sec
2/4 Test #2: test_d_stress ....................   Passed    1.65 sec
3/4 Test #3: verify_parallel_bmssp ............   Passed    0.36 sec
4/4 Test #4: test_parallel_edge_cases .........   Passed    1.15 sec
100% tests passed out of 4 | Total Test time (real) = 3.42 sec
```

*(Ghi chú compiler warning: 0 warning trong toàn bộ các file đã viết/chỉnh sửa; các warning còn lại chỉ nằm trong header upstream gốc `bmssp-worst-case.hpp` và biến `last_dist` của `MutexFrontierD` thuộc phần Người 1 không chỉnh sửa.)*

---

## 4. Kết Quả Benchmark A/B Thực Nghiệm (`bench_parallel_bmssp.exe`)

- **Phương pháp luận:** 2 warmup + 7 mẫu đo tuần tự, lấy **trung vị (`Median ms`)**, loại trừ thời gian tạo đồ thị. Mọi solver được đối chiếu `dijkstra_sequential` (`tol = 1e-9`) và `bmssp_parallel` được kiểm tra toàn bộ bất biến cây `pred`.
- **Trạng thái xác minh:** **24 / 24 cấu hình `OK` (`MaxDiff = 0.00e+00`)**.

### 4.1. Sparse Random (`n = 20.000, m = 100.000, max_out_degree = 16, seed = 2026`)

| Solver | Threads | Median (ms) | Min (ms) | Max (ms) | vs Dijkstra | Self-Scale | MaxDiff | Status |
|---|---:|---:|---:|---:|---:|---:|---:|:---:|
| `dijkstra_sequential` | 1 | 7,665 | 7,389 | 9,315 | 1,00× | — | `0.00e+00` | OK |
| `bmssp_seq` | 1 | 60,888 | 54,543 | 62,550 | 0,13× | — | `0.00e+00` | OK |
| `bmssp_parallel (Mutex)` | 1 | 28,400 | 26,180 | 32,315 | 0,27× | 1,00× | `0.00e+00` | OK |
| `bmssp_parallel (Mutex)` | 4 | 50,440 | 48,565 | 56,061 | 0,15× | 0,56× | `0.00e+00` | OK |
| `bmssp_parallel (Mutex)` | 8 | 56,868 | 53,580 | 58,383 | 0,13× | 0,50× | `0.00e+00` | OK |
| `bmssp_parallel (Concurrent)` | 1 | 26,747 | 25,541 | 30,228 | 0,29× | 1,00× | `0.00e+00` | OK |
| `bmssp_parallel (Concurrent)` | 4 | 32,042 | 28,499 | 32,825 | 0,24× | 0,83× | `0.00e+00` | OK |
| `bmssp_parallel (Concurrent)` | 8 | 30,978 | 29,912 | 32,377 | 0,25× | 0,86× | `0.00e+00` | OK |

- **Tỷ số A/B (`Mutex / Concurrent`):** 1T = `1,06×` | 4T = **`1,57×`** | 8T = **`1,84×`**.

### 4.2. 2D Grid (`150 × 150, n = 22.500, m = 89.400, max_out_degree = 4`)

| Solver | Threads | Median (ms) | Min (ms) | Max (ms) | vs Dijkstra | Self-Scale | MaxDiff | Status |
|---|---:|---:|---:|---:|---:|---:|---:|:---:|
| `dijkstra_sequential` | 1 | 4,102 | 3,798 | 4,552 | 1,00× | — | `0.00e+00` | OK |
| `bmssp_seq` | 1 | 48,714 | 45,265 | 50,857 | 0,08× | — | `0.00e+00` | OK |
| `bmssp_parallel (Mutex)` | 1 | 19,524 | 17,939 | 20,914 | 0,21× | 1,00× | `0.00e+00` | OK |
| `bmssp_parallel (Mutex)` | 4 | 18,570 | 17,970 | 19,342 | 0,22× | 1,05× | `0.00e+00` | OK |
| `bmssp_parallel (Mutex)` | 8 | 19,893 | 19,406 | 21,951 | 0,21× | 0,98× | `0.00e+00` | OK |
| `bmssp_parallel (Concurrent)` | 1 | 31,183 | 29,393 | 33,418 | 0,13× | 1,00× | `0.00e+00` | OK |
| `bmssp_parallel (Concurrent)` | 4 | 34,085 | 31,049 | 47,084 | 0,12× | 0,91× | `0.00e+00` | OK |
| `bmssp_parallel (Concurrent)` | 8 | 31,938 | 30,140 | 33,701 | 0,13× | 0,98× | `0.00e+00` | OK |

- **Tỷ số A/B (`Mutex / Concurrent`):** 1T = `0,63×` | 4T = `0,54×` | 8T = `0,62×`.

### 4.3. Hub Graph (`n = 20.000, deg(0) = 10.000 + 80.000` cạnh nền sparse)

| Solver | Threads | Median (ms) | Min (ms) | Max (ms) | vs Dijkstra | Self-Scale | MaxDiff | Status |
|---|---:|---:|---:|---:|---:|---:|---:|:---:|
| `dijkstra_sequential` | 1 | 6,581 | 5,916 | 6,781 | 1,00× | — | `0.00e+00` | OK |
| `bmssp_seq` | 1 | 106,735 | 100,209 | 118,760 | 0,06× | — | `0.00e+00` | OK |
| `bmssp_parallel (Mutex)` | 1 | 28,251 | 24,637 | 31,814 | 0,23× | 1,00× | `0.00e+00` | OK |
| `bmssp_parallel (Mutex)` | 4 | 41,834 | 40,874 | 44,875 | 0,16× | 0,68× | `0.00e+00` | OK |
| `bmssp_parallel (Mutex)` | 8 | 46,997 | 44,929 | 49,575 | 0,14× | 0,60× | `0.00e+00` | OK |
| `bmssp_parallel (Concurrent)` | 1 | 28,559 | 25,269 | 32,576 | 0,23× | 1,00× | `0.00e+00` | OK |
| `bmssp_parallel (Concurrent)` | 4 | 31,503 | 29,338 | 33,127 | 0,21× | 0,91× | `0.00e+00` | OK |
| `bmssp_parallel (Concurrent)` | 8 | 32,649 | 30,029 | 33,729 | 0,20× | 0,87× | `0.00e+00` | OK |

- **Tỷ số A/B (`Mutex / Concurrent`):** 1T = `0,99×` | 4T = **`1,33×`** | 8T = **`1,44×`**.

### 4.4. Phân tích trung thực về hiệu năng
1. **Ưu thế của `ConcurrentFrontierD` trên tải đồng thời cao:** Trên Sparse 20K và Hub 20K ở 4–8 luồng, `MutexFrontierD` bị suy giảm nặng (`0,50×–0,68×`) do tranh chấp khóa độc quyền toàn cục; `ConcurrentFrontierD` tránh được điểm nghẽn này và chạy nhanh hơn `MutexFrontierD` **`1,33× – 1,84×`** (và nhanh hơn `bmssp_seq` `1,97× – 3,27×`).
2. **Trường hợp 2D Grid frontier hẹp:** Với $n = 22.500$ bậc đều 4, hầu hết các pha relax có số cạnh hoạt động dưới ngưỡng `DEFAULT_GRANULARITY_CUTOFF = 2048` (chạy tuần tự 1 luồng), nên chi phí cấu trúc B-Link Skip-List + EBR của `ConcurrentFrontierD` lớn hơn `std::multimap` đơn luồng (`0,54×–0,63×`).
3. **So sánh với Dijkstra:** Trên cả 3 đồ thị cỡ 20K đỉnh, `dijkstra_sequential` vẫn nhanh nhất (`4,10–7,67 ms`), phù hợp với các phân tích lý thuyết và thực nghiệm trước đó của dự án về hằng số ẩn và độ dài đường đi ngắn ở kích thước $n \sim 10^4$.

---

## 5. Giới Hạn Cần Ghi Nhận Minh Bạch

1. **Sanitizer (TSan / ASan):** Toolchain `w64devkit` GCC `16.2.0` trên Windows không đi kèm `libtsan`/`libasan`, nên chưa chạy `-fsanitize=thread` hoặc `-fsanitize=address` trong phiên này. Cần chạy trên Linux/WSL2 với lệnh:
   ```bash
   cmake -S bmssp_parallel -B bmssp_parallel/build_tsan -DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1"
   cmake --build bmssp_parallel/build_tsan -j
   ctest --test-dir bmssp_parallel/build_tsan --output-on-failure
   ```
2. **Phạm vi kiểm chứng:** 4 bộ test và 240 lượt stress đa luồng là bằng chứng thực nghiệm mạnh nhưng không thay thế chứng minh toán học tổng quát cho toàn bộ bất biến và cận tiệm cận $O(m \log^{2/3} n)$.
3. **Trạng thái Git:** Mọi thay đổi hiện nằm cục bộ trên nhánh `fix/parallel-bmssp-remediation`, chưa commit và chưa push lên remote GitHub.
