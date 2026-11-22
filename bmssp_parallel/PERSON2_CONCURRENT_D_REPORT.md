# Báo Cáo Triển Khai & Kiểm Định Phần Người 2: Cấu Trúc Dữ Liệu Đồng Thời $\mathcal{D}$

**Thành viên phụ trách:** Nguyễn Công Việt (Người 2)  
**Ngày cập nhật:** 18/11/2026 (Asia/Saigon)  
**Nhánh làm việc / Xuất bản:** `feat/concurrent-d` $\rightarrow$ nhánh GitHub `Viet` (`https://github.com/23001939/bmssp/tree/Viet`)  
**Nền tham chiếu:** Commit `874775b` trên nhánh `main` (hạ tầng Người 1)  
**Trạng thái DoD Người 2:** **95%** *(đạt 3/4 tiêu chí DoD trên Windows; còn bước chạy ThreadSanitizer `-fsanitize=thread` / ASan / Valgrind cần thực hiện trên WSL2/Linux do MinGW `w64devkit` không hỗ trợ `-ltsan`/`-lasan`)*.

---

## 1. Tóm Tắt Điều Hành

Phần Người 2 hiện thực hóa cấu trúc dữ liệu phân tầng **$\mathcal{D} = (\mathcal{D}_0, \mathcal{D}_1)$** theo **Lemma 3.3** của bài báo *"Breaking the Sorting Barrier for Directed Single-Source Shortest Paths"* (arXiv:2504.17033v2) thành lớp **`ConcurrentFrontierD : public IFrontierD`**, thay thế bản mockup khóa thô `MutexFrontierD` (`std::mutex` + `std::multimap`) mà **không sửa đổi bất kỳ tệp nào thuộc phần Người 1** (`include/core/*`, `src/core/*`, `include/baseline/*`, `src/baseline/*`, `tests/test_person1_comprehensive.cpp`).

### Kết quả nổi bật đo trực tiếp trên phần cứng (12 luồng CPU logic, `g++ 16.2.0`)
1. **Độ đúng & Chịu tải tương tranh (`test_d_stress.exe`):** **1.472.524 / 1.472.524 checks PASS** (`14.361,77 ms`), bao gồm bài stress-test **32 luồng thực thi 10.000.000 thao tác** (`9.750.000` `insert`, `125.000` `pull`, `125.000` `batch_prepend` qua `10` vòng `init/clear`) với `0` deadlock, `0` crash và tính đơn điệu $B_1 \le B_2 \le \dots \le B$.
2. **Eager Cleanup giảm chia tách block:** Trên đồ thị dày `N = 2.048` đỉnh / `200.000` cạnh (`M = 32`, `8` luồng), số lần `SPLIT` giảm từ `683` (khi tắt) xuống **`307`** (khi bật) — đạt mức giảm **`55,05%`** (vượt ngưỡng DoD $\ge 50\%$). Trong Benchmark A/B, tỷ lệ cứu split đạt khoảng `70%–74%` trên tổng số sự kiện tràn block.
3. **So sánh Trước (`MutexFrontierD`) vs Sau (`ConcurrentFrontierD`):**
   - **Benchmark A (1.000.000 lượt relax + `insert`):** Tại `8` luồng, thời gian giảm từ `726,42 ms` (`Mutex`) xuống **`34,17 ms`** (`Concurrent`, đạt **`29,27 Mops/s`**) — nhanh hơn **`21,26×`** so với bản trước song song và tăng tốc nội bộ **`4,51×`** so với chạy `1` luồng.
   - **Benchmark B (Vòng lặp cắt lát đầy đủ `pull` + `relax/insert` + `batch_prepend`, đồ thị `N=50.000, E=500.000`):** Tại `8` luồng, tổng thời gian giảm từ `201,22 ms` xuống **`41,33 ms`** (nhanh hơn **`4,87×`**; riêng pha `insert` giảm từ `183,61 ms` xuống **`11,78 ms`**, nhanh hơn **`15,59×`**). Khoảng cách cuối cùng trên mọi mức luồng khớp **100%** với `dijkstra_sequential` oracle.

---

## 2. Kiến Trúc & Các Tệp Bàn Giao

### 2.1. Danh sách tệp tạo mới / cập nhật
| Tệp | Vai trò | Ghi chú phân quyền |
|---|---|---|
| `bmssp_parallel/include/data_structure/d0_deque.hpp`<br>`bmssp_parallel/src/data_structure/d0_deque.cpp` | Tầng $\mathcal{D}_0$ (`D0Deque`) dùng `std::deque<D0Block>` | Thuộc Người 2; bãi bỏ Treiber Stack |
| `bmssp_parallel/include/data_structure/d1_skiplist.hpp`<br>`bmssp_parallel/src/data_structure/d1_skiplist.cpp` | Tầng $\mathcal{D}_1$ (`D1SkipList`): B-Link Skip-List, `BlockSpinlock`, Eager Cleanup, Pool 64 shard, EBR | Thuộc Người 2 |
| `bmssp_parallel/include/data_structure/concurrent_frontier_d.hpp`<br>`bmssp_parallel/src/data_structure/concurrent_frontier_d.cpp` | Lớp `ConcurrentFrontierD : public IFrontierD` gộp $\mathcal{D}_0, \mathcal{D}_1$ | Thuộc Người 2 |
| `bmssp_parallel/tests/test_d_stress.cpp` | Bộ kiểm thử đơn vị, đơn điệu, Eager Cleanup & stress 32T / 10M ops | Phục vụ nghiệm thu DoD |
| `bmssp_parallel/benchmarks/bench_person2_d.cpp` | Benchmark đối chứng A/B: `MutexFrontierD` vs `ConcurrentFrontierD` | Đo tuần tự cùng tải & seed |
| `bmssp_parallel/CMakeLists.txt` | Bổ sung thư viện `bmssp_data_structure` và target `test_d_stress`, `bench_person2_d` | Giữ nguyên `bmssp_core` & `test_person1` |

### 2.2. Các quyết định kỹ thuật & bất biến tương tranh đã triển khai
1. **Tầng $\mathcal{D}_0$ (`D0Deque`):**
   - Dùng `std::deque<D0Block>` (mỗi block $\le M$ phần tử đã sắp tăng dần).
   - `batch_prepend(K)` lọc `!std::isnan(dist) && dist <= B_`; dùng đường nhanh $O(|K|)$ khi `K` đã sắp tăng dần và `K.back().dist <= blocks_.front().items.front().dist`, đẩy block từ cuối về đầu qua `push_front` để giữ phần tử nhỏ nhất ở `front()`. Nếu có chồng lấn khoảng giá trị hoặc đầu vào chưa sắp, tự động trộn phần đầu chồng lấn để bảo toàn thứ tự toàn cục.
2. **Tầng $\mathcal{D}_1$ (`D1SkipList`) — B-Link Skip-List & Tránh Deadlock:**
   - **Đọc không khóa (Lock-free traversal):** `insert()` duyệt Skip-List từ `current_max_level_` xuống tầng `0` bằng `acquire`/`release` trên `upper_bound` và `forward[lvl]`.
   - **Lazy Deletion + Không giữ 2 khóa block:** Khi khoảng cách đỉnh `v` được cải thiện, luồng chỉ khóa đúng **1** `BlockSpinlock` (64-byte aligned, TTAS + `_mm_pause()`) của block đích và chèn `(v, dist)`; không tìm xóa bản ghi cũ ở block khác.
   - **Phục hồi chia tách B-Link (`SPLIT`):** Dưới trình tự khóa `BlockNode::spinlock -> tower_mtx_`, `right->upper_bound` và `right->forward[0]` được gán trước khi gắn `curr->forward[0] = right` (release), và **sau đó** mới hạ `curr->upper_bound = median_ub` (release). Luồng `insert` sau khi chiếm `spinlock` luôn kiểm tra lại `curr->retired` và `dist <= curr->upper_bound`; nếu block vừa bị tách, nhả khóa và đi tiếp sang `forward[0]` trong $O(1)$. Trường hợp có nhiều block cùng giá trị `upper_bound` được phân biệt thứ tự toàn phần bằng `sub_order` kèm cơ chế `relabel_ub_run_locked`.
   - **Eager Cleanup trước Split:** Khi block tràn $> M$, quét lọc ngay các bản ghi lỗi thời (`entry.dist > state_arr[entry.v].load(acquire).dist`). Chỉ khi số phần tử còn lại vẫn $> M$ mới dùng `std::nth_element` chia đôi block.
   - **Thu hồi bộ nhớ an toàn & không leak (`ThreadLocalBlockPool` + EBR):** Quản lý cấp phát `BlockNode` qua 64 shard `ThreadLocalBlockPool<BlockNode, 1024>` thuộc sở hữu của `D1SkipList` (mỗi node ghi `pool_id` để thu hồi đúng shard, giải phóng 100% ở `clear()` / `~D1SkipList()`). Các block rỗng không phải block đuôi bị `drain_valid_up_to` đánh dấu `retired` và **gỡ đồng bộ ngay khỏi mọi tầng Skip-List dưới `tower_mtx_` trước khi nhả `spinlock`**, rồi mới thu hồi node qua Epoch-Based Reclamation (`ReaderEpochRegistry` + `ReaderGuard` với hàng rào `seq_cst`).
3. **Hợp nhất trong `ConcurrentFrontierD`:**
   - `insert()` đi thẳng vào `D1SkipList` mà không chạm `pull_mtx_`.
   - `pull(S_i, B_i)` rút tối đa `M_ + 1` đỉnh hợp lệ phân biệt từ đầu $\mathcal{D}_0$ và $\mathcal{D}_1$ (đối chiếu `state_arr_` và giữ `dist` nhỏ nhất cho mỗi đỉnh), trả tối đa `M_` đỉnh nhỏ nhất vào `S_i`, trả `B_i = B_` nếu cả hai tầng đều cạn hoặc `remaining[0].dist` nếu còn phần tử, và đẩy phần dư đã sắp trở lại đầu $\mathcal{D}_0$ / $\mathcal{D}_1$.

---

## 3. Kết Quả Kiểm Chứng Chi Tiết

### 3.1. Môi trường thực thi & Biên dịch
- **Phần cứng:** CPU `12` luồng logic (`std::thread::hardware_concurrency() == 12`, kiến trúc 6 nhân vật lý / 12 luồng SMT).
- **Trình biên dịch:** `g++.exe (GCC) 16.2.0` (`D:/w64devkit/bin/c++.exe`), CMake `4.4.3` (`MinGW Makefiles`).
- **Cờ biên dịch:** `-std=c++20 -O3 -march=native -mcx16 -Wall -Wextra -fopenmp`.
- **Trạng thái biên dịch:** 0 warnings, 0 errors trên tất cả các target (`bmssp_core`, `bmssp_data_structure`, `test_person1`, `test_d_stress`, `bench_person2_d`).

### 3.2. Kết quả Regression Người 1 & Stress Test Người 2
- **`test_person1.exe`:** PASS **153.932 / 153.932 checks** (`9.38 ms`) qua 6 test suite.
- **`test_d_stress.exe`:** PASS **1.472.524 / 1.472.524 checks** (`14.361,77 ms`):
  - **Suite 1 (Edge cases):** `M=1` (clamp `M=0`), `NaN`, vượt cận `B_`, `B = INF_DIST`, khóa trùng, tie khoảng cách, `pull()` khi toàn rác trả `false` (`B_i = B_`), lặp `20` chu kỳ `init/clear` — PASS.
  - **Suite 2 (Phased BMSSP 8T & 16T):**
    - `2A` (phân tầng chính xác): `244` pulls, `7.801` đỉnh trích xuất đúng 100% (cả `8T` và `16T`).
    - `2B` (đồ thị có chu trình): `124` pulls (`8T`: `978` overflows, `291` splits, `687` saved, `2.039` stale on pull) và `126` pulls (`16T`: `1.000` overflows, `296` splits, `704` saved, `2.082` stale on pull); $B_1 \le B_2 \le \dots \le B$ và khớp 100% với `dijkstra_sequential` oracle.
  - **Suite 3 (Eager Cleanup trên đồ thị dày `N=2.048, E=200.000, M=32, 8T`):**
    - `Eager Cleanup OFF`: `overflow_events = 683`, `actual_splits = 683`
    - `Eager Cleanup ON`: `overflow_events = 1.396`, `actual_splits = 307`, `eager_cleanup_saved_splits = 1.089`
    - **Mức giảm `SPLIT`: `55,05%` (`683 -> 307`)** — đạt DoD $\ge 50\%$.
  - **Suite 4 (Stress 32 luồng / 10.000.000 thao tác):**
    - `10` rounds × `32` luồng = `10.000.000` ops (`9.750.000` inserts, `125.000` pulls, `125.000` batch_prepends) trong **`14.226,95 ms`** (`0,70 Mops/s`).
    - `overflow_events = 76.820`, `actual_splits = 34.002`, `saved_splits = 42.818`, `stale_ins = 28`, `stale_pull = 1.089.102`.
    - Kiểm tra sau mỗi vòng: `B_i` đơn điệu tăng, trạng thái đỉnh hữu hạn/đúng phạm vi, `frontier.clear()` dọn sạch.

---

## 4. So Sánh Hiệu Năng Trước vs Sau Phần Người 2

Các phép đo dưới đây được chạy **tuần tự** trên cùng máy, cùng cờ biên dịch, cùng tập sinh ngẫu nhiên (1 lần warmup + 3 lần đo lấy trung vị).

### 4.1. Benchmark A — Pha `relax + insert` đồng thời (`1.000.000` lượt relax, `20` wave × `50.000`, `N = 50.000`, `M = 64`, `B = 100.000`, Seed `20261118`)

| Số luồng | `MutexFrontierD` (ms) | `Mutex` (Mop/s) | `Mutex` Scale | `ConcurrentFrontierD` (ms) | `Concurrent` (Mop/s) | `Concurrent` Scale | Tăng tốc vs `Mutex` | Overflows / Splits / Saved |
|---:|---:|---:|---:|---:|---:|---:|---:|---|
| **1** | 367,70 | 2,72 | 1,00× | 154,05 | 6,49 | 1,00× | **2,39×** | `29.241 / 7.681 / 21.560` |
| **2** | 792,83 | 1,26 | 0,46× | 95,75 | 10,44 | 1,61× | **8,28×** | `29.420 / 7.660 / 21.760` |
| **4** | 727,94 | 1,37 | 0,51× | 53,81 | 18,58 | 2,86× | **13,53×** | `29.432 / 7.670 / 21.762` |
| **8** | 726,42 | 1,38 | 0,51× | **34,17** | **29,27** | **4,51×** | **21,26×** | `29.447 / 7.673 / 21.774` |
| **12** | 696,61 | 1,44 | 0,53× | 37,50 | 26,67 | 4,11× | **18,58×** | `29.480 / 7.663 / 21.817` |
| **32** *(oversubscribed)* | 836,71 | 1,20 | 0,44× | 75,96 | 13,16 | 2,03× | **11,01×** | `29.250 / 7.652 / 21.598` |

*Số lượng insert thành công tương đương ở cả hai bản (~604.099–604.494 lần).*

### 4.2. Benchmark B — Vòng lặp cắt lát đầy đủ (`pull` + `relax/insert` + `batch_prepend`, đồ thị `N = 50.000`, `E = 500.000`, `M = 64`, trọng số `[1.0, 100.0]`, Seed `20261119`, `779` lần `pull`)

| Số luồng | `Mutex` Tổng (ms) | `Mutex` Insert / Pull (ms) | `Concurrent` Tổng (ms) | `Concurrent` Insert / Pull (ms) | Tăng tốc tổng vs `Mutex` | `Concurrent` Scale |
|---:|---:|---|---:|---|---:|---:|
| **1** | 71,27 | `61,57` / `9,40` | 59,08 | `32,63` / `26,31` | **1,21×** | 1,00× |
| **2** | 120,93 | `109,68` / `10,93` | 51,26 | `22,60` / `28,48` | **2,36×** | 1,15× |
| **4** | 175,67 | `159,81` / `15,52` | 49,58 | `19,12` / `30,31` | **3,54×** | 1,19× |
| **8** | 201,22 | `183,61` / `17,28` | **41,33** | **`11,78`** / `29,37` | **4,87×** | **1,43×** |
| **12** | 204,46 | `188,93` / `15,19` | 59,27 | `26,95` / `32,14` | **3,45×** | 1,00× |

- **Xác thực đúng đắn:** 100% khoảng cách cuối của mọi lần chạy `MutexFrontierD` và `ConcurrentFrontierD` trên các mức 1–12 luồng đều khớp tuyệt đối với `dijkstra_sequential` oracle.
- **Thống kê `FrontierStats` (`Concurrent` tại trung vị 1–12T):** `779` pulls, `5.829–5.923` overflows, `1.733–1.746` splits thực tế, **`4.083–4.190` splits được cứu nhờ Eager Cleanup**, `8.953–9.420` bản ghi rác được loại lúc `pull`.

---

## 5. Điểm Nghẽn Phát Hiện, Giới Hạn & Việc Cần Phối Hợp Nhóm

### 5.1. Hai điểm nghẽn / giới hạn quan sát được
1. **Chi phí pha `pull()` (`26,31–32,14 ms` vs `9,40–17,28 ms` ở `MutexFrontierD`):**  
   `ConcurrentFrontierD::pull()` thực hiện đúng ngữ nghĩa Lemma 3.3 (đối chiếu `AtomicVertexState`, khử trùng lặp đỉnh giữa $\mathcal{D}_0$ và $\mathcal{D}_1$, trả phần dư về lại cấu trúc, kèm quét EBR `seq_cst` khi gỡ block rỗng), nên nặng hơn `MutexFrontierD::pull()` vốn chỉ pop trực tiếp từ `std::multimap`. Khi tích hợp vào `BMSSP` đa tầng với kích thước $M$ lớn hơn ở tầng trên, tỷ trọng chi phí `pull` trên mỗi vòng lặp sẽ nhỏ đi.
2. **Suy giảm tại `12` luồng SMT trong Benchmark B (`59,27 ms` so với `41,33 ms` ở `8` luồng):**  
   Mỗi vòng của Benchmark B chỉ rút `M = 64` đỉnh (~`640` cạnh), thấp hơn ngưỡng cắt độ hạt $\Theta_{\text{cutoff}} = 2048$ cạnh trong thiết kế. Chi phí barrier + SMT ở 12 luồng lớn hơn lượng việc song song của mỗi pha nhỏ, xác nhận tầm quan trọng của `Granularity Cutoff` mà Người 3 sẽ áp dụng.
3. **Thiếu môi trường ThreadSanitizer/ASan trên Windows:**  
   `w64devkit` GCC 16.2.0 thiếu `-ltsan` và `-lasan`, nên tiêu chí **DoD #4** (0 cảnh báo TSan / Valgrind) chưa chạy trên Windows và cần Người 4 (Dương Văn Tài) chạy xác nhận trong **WSL2/Linux** trước khi merge vào `main`.

### 5.2. Lưu ý tích hợp cho Người 1, Người 3 và Người 4
- **Truyền mảng trạng thái đỉnh (`AtomicVertexState*`):** Vì `IFrontierD::init(M, B)` hiện chưa nhận mảng trạng thái toàn cục, khi khởi tạo `ConcurrentFrontierD` trong `src/main.cpp` cần gọi constructor `ConcurrentFrontierD(states.data(), n)` hoặc `frontier.set_state_array(states.data(), n)`. Nếu không truyền (`nullptr`), cấu trúc vẫn chạy an toàn nhưng tắt cơ chế lọc rác dựa trên `AtomicVertexState`.
- **Quy ước `B_i` khi rỗng:** `ConcurrentFrontierD::pull()` trả `B_i = B_` khi cấu trúc cạn (đúng Lemma 3.3 và `batchPQ`), khác với `MutexFrontierD::pull()` trả `last_dist` khi vừa cạn hết phần tử.

---

## 6. Lệnh Tái Lập (PowerShell từ `C:\ttss`)

```powershell
cmake -S bmssp_parallel -B bmssp_parallel/build -G "MinGW Makefiles"
cmake --build bmssp_parallel/build -j 8
.\bmssp_parallel\build\test_person1.exe
.\bmssp_parallel\build\test_d_stress.exe
.\bmssp_parallel\build\bench_person2_d.exe
```

*(Tùy chọn chạy TSan trên WSL2 / Linux cho DoD #4):*
```bash
g++ -std=c++20 -O1 -g -fsanitize=thread -mcx16 -fopenmp -Ibmssp_parallel/include -Ibmssp_parallel/include/baseline/external -Ibmssp_parallel/include/baseline/upstream/include bmssp_parallel/src/core/graph.cpp bmssp_parallel/src/baseline/bmssp_seq.cpp bmssp_parallel/src/data_structure/*.cpp bmssp_parallel/tests/test_d_stress.cpp -o test_d_stress_tsan && ./test_d_stress_tsan
```
