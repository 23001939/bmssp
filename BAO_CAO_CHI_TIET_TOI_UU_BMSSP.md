# Báo Cáo Chi Tiết Các Hạng Mục Tối Ưu Hóa Mã Nguồn Parallel BMSSP

- **Ngày lập báo cáo:** 24/11/2026 (Asia/Saigon)
- **Mốc mã nguồn gốc đối chiếu (Trước tối ưu):** Nhánh `origin/parallel-bmssp` tại commit `2cd0b3c` (24/11/2026) và bộ sinh đồ thị kiểm chuẩn từ nhánh `origin/feat/person4-qa` tại commit `c76a215` (`seed = 20261122`).
- **Môi trường biên dịch & thực nghiệm:**
  - **Vi xử lý:** AMD Ryzen 5 7640HS (6 nhân vật lý / 12 luồng xử lý logic SMT).
  - **Hệ điều hành:** Windows / PowerShell.
  - **Trình biên dịch:** GCC `16.2.0` (`-std=c++20 -O3 -march=native -mcx16 -Wall -Wextra -fopenmp`).
- **Tài liệu & dữ liệu liên kết:**
  - Báo cáo số liệu benchmark 100K–2M đỉnh: [FINAL_VS_DIJKSTRA_1M_2M_REPORT.md](file:///c:/ttss/bmssp_parallel/FINAL_VS_DIJKSTRA_1M_2M_REPORT.md)
  - Dữ liệu CSV thô: [final_benchmark_1m_2m.csv](file:///c:/ttss/bmssp_parallel/final_benchmark_1m_2m.csv)
  - Biểu đồ đối sánh tốc độ: [final_vs_dijkstra_1m_2m.png](file:///c:/ttss/bmssp_parallel/final_vs_dijkstra_1m_2m.png)

---

## 1. Khẳng Định Bảo Toàn 100% Bản Chất Thuật Toán BMSSP

Toàn bộ các tối ưu hóa được trình bày trong tài liệu này là **tối ưu hóa kỹ thuật cài đặt cấp hệ thống (cấu trúc dữ liệu nội bộ, quản lý cấp phát bộ nhớ, cơ chế đồng bộ hóa luồng và vi kiến trúc lệnh nguyên tử CPU)**. Bản chất lý thuyết của thuật toán **BMSSP** trong bài báo gốc *Breaking the Sorting Barrier for Directed Single-Source Shortest Paths* (Ran Duan, Jiayi Mao, Xiao Mao, Xinkai Shu, Longhui Yin — STOC 2025, [2504.17033v2.pdf](file:///c:/ttss/2504.17033v2.pdf)) được giữ nguyên vẹn 100%:

1. **Không thay đổi công thức tham số lý thuyết** ([bmssp.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/bmssp.cpp#L714-L717)):
   - Bước lặp tìm chốt: $k = \lfloor (\log_2 n)^{1/3} \rfloor$
   - Hệ số phân tầng khoảng cách: $t = \lfloor (\log_2 n)^{2/3} \rfloor$
   - Chiều cao cây đệ quy: $l_{\max} = \lceil \frac{\log_2 n}{t} \rceil$
   - Kích thước khối ở tầng đệ quy $l$: $M_l = 2^{(l-1)t}$
   - Hạn ngạch thực thi từng phần (Partial Execution Quota) ở tầng $l$: $\text{quota}_l = k \cdot 2^{lt}$
2. **Không thay đổi cấu trúc 3 thủ tục cốt lõi và cấu trúc dữ liệu 2 tầng:**
   - **Algorithm 1 (`FindPivots` — [find_pivots.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/find_pivots.cpp)):** Vẫn thực hiện đúng $k$ bước thư giãn cạnh Bellman–Ford có cận trên $B$, dừng sớm khi $|W| > k|S|$, dựng rừng ngắn nhất tạm thời và chỉ giữ lại các đỉnh gốc có kích thước cây con $\ge k$ làm tập chốt $P$ ($|P| \le |W|/k$).
   - **Algorithm 2 (`BaseCase` — [base_case.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/base_case.cpp)):** Ở tầng đáy $l = 0$, vẫn chỉ mở rộng cục bộ tối đa $k + 1$ đỉnh từ nguồn đơn lẻ và thu hẹp biên trả về $B'$ theo đỉnh thứ $(k + 1)$.
   - **Algorithm 3 (`BMSSP` — [bmssp.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/bmssp.cpp)) & Lemma 3.3 ($\mathcal{D} = (\mathcal{D}_0, \mathcal{D}_1)$ — [concurrent_frontier_d.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/concurrent_frontier_d.cpp)):** Vẫn duy trì vòng lặp cắt lát khoảng cách (Distance-Sliced Loop): rút tập con $S_i$ ($|S_i| \le M_l$) và biên phân cách $B_i$ từ $\mathcal{D}$, gọi đệ quy xuống tầng $l - 1$, thư giãn song song các cạnh đi ra từ tập đỉnh hoàn thành mới, đưa các đỉnh $\ge B_i$ vào $\mathcal{D}_1$ và gom lô $K$ ($< B_i$) đẩy vào đầu $\mathcal{D}_0$ qua `batch_prepend(K)`.
3. **Không dùng nhánh fallback sang Dijkstra và không chỉnh sửa solver đối chứng:**
   - Hoàn toàn không có bất kỳ điều kiện rẽ nhánh nào gọi Dijkstra thay cho BMSSP.
   - Tệp đối chứng [bmssp_seq.cpp](file:///c:/ttss/bmssp_parallel/src/baseline/bmssp_seq.cpp) (`dijkstra_sequential`) được giữ nguyên 100% không sửa một ký tự nào để đảm bảo tính khách quan của phép đo.

---

## 2. Phân Tích Chi Tiết 6 Hạng Mục Đã Tối Ưu Trong Mã Nguồn

### Hạng mục 1: Chuyên biệt hóa trường hợp biên $M = 1$ (tầng `level = 1`) trong `ConcurrentFrontierD`

- **Tệp liên quan:** [concurrent_frontier_d.hpp](file:///c:/ttss/bmssp_parallel/include/data_structure/concurrent_frontier_d.hpp), [concurrent_frontier_d.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/concurrent_frontier_d.cpp)
- **Vấn đề trong code cũ (`2cd0b3c`):**
  - Ở tầng đệ quy `level = 1`, công thức lý thuyết cho $M_1 = 2^{(1-1)t} = 2^0 = 1$ (tức mỗi khối chỉ chứa tối đa **1 đỉnh**).
  - Trên đồ thị $1.000.000$ đỉnh ($k = 2$), gần như toàn bộ các đỉnh đều đi qua tầng `level = 1` (~400.000 lượt gọi `pull()`). Việc dùng danh sách nhảy khối `D1SkipList` cho dung lượng khối $M = 1$ khiến **mọi thao tác chèn đỉnh thứ 2 đều kích hoạt tràn khối (`handle_overflow_locked`)**: phải cấp phát node mới, khóa `tower_mtx_`, tìm kiếm lại từ `head_` để gán `sub_order`, nối tháp con trỏ; sau đó `pull()` lại gỡ node (`unlink_node_locked`), tăng `global_epoch` (`seq_cst` fence) và quét 512 khe EBR cho từng đỉnh lẻ.
- **Cách tối ưu:**
  - Khi `ConcurrentFrontierD::init(M, B)` nhận $M = 1$, bật cờ `use_m1_heap_ = true` sử dụng một hàng đợi ưu tiên nhị phân phẳng (`std::vector<VertexDist> m1_heap_`) được bảo vệ bởi khóa nhẹ `BlockSpinlock`, kết hợp lọc lười biếng phần tử cũ (`stale`), gom đỉnh trùng ngay tại đỉnh heap và ghi nhận đầy đủ bộ đếm QA (`stale_dropped_on_insert`, `stale_dropped_on_pull`).
  - Đối với mọi tầng có $M > 1$ (ví dụ $M_2 = 128$, $M_3 = 16.384$), thuật toán vẫn chạy trên cấu trúc hai tầng `D0Deque` + `D1SkipList`.
- **Chứng minh bảo toàn thuật toán:**
  - Theo Lemma 3.3 của bài báo gốc, chi phí lý thuyết là $\mathcal{O}(\max\{1, \log(N/M)\})$ cho `Insert` và $\mathcal{O}(|S'|)$ cho `Pull`, trong đó $|S'| \le M$ và $B_i$ là cận dưới của phần tử nhỏ nhất còn lại trong $\mathcal{D}$.
  - Khi thay $M = 1$ vào công thức Lemma 3.3, khối kích thước $1$ suy biến chính xác về thứ tự toàn phần với cận $\mathcal{O}(\log N)$ cho `Insert` và $\mathcal{O}(1)$ khấu hao (amortized) cho `Pull`: tập trả về $S_i$ bắt buộc gồm đúng 1 đỉnh hợp lệ có khoảng cách nhỏ nhất toàn cục và $B_i$ là khoảng cách hợp lệ nhỏ nhất kế tiếp (hoặc $B$ nếu cạn). Ngữ nghĩa đầu vào/đầu ra trùng khớp tuyệt đối 100%.

---

### Hạng mục 2: Tối ưu `D.Pull` cho $M > 1$ — Bảng băm `epoch` $\mathcal{O}(1)$, `std::nth_element`, con trỏ đầu `D0Deque` và gom lô EBR

- **Tệp liên quan:** [concurrent_frontier_d.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/concurrent_frontier_d.cpp), [d0_deque.hpp](file:///c:/ttss/bmssp_parallel/include/data_structure/d0_deque.hpp), [d0_deque.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/d0_deque.cpp), [d1_skiplist.hpp](file:///c:/ttss/bmssp_parallel/include/data_structure/d1_skiplist.hpp), [d1_skiplist.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/d1_skiplist.cpp)
- **Vấn đề trong code cũ (`2cd0b3c`):**
  1. Mỗi lượt `pull()` gọi hàm `deduplicate_min_dist` (`std::sort` theo ID đỉnh) lặp lại trong vòng `while` của `D0`, lặp lại trong vòng `while` của `D1`, rồi lại `std::sort` lần thứ 3 khi trộn `merged_buf_`, và cuối cùng gọi full `std::sort` lần thứ 4 theo khoảng cách ($\mathcal{O}(M \log M)$).
  2. `D0Deque` rút nguyên cả khối rồi `pop_front()` (giải phóng bộ nhớ `std::vector`), sau đó phần dư `rem0_buf_` lại cấp phát `std::vector` mới để đẩy ngược vào đầu `D0`.
  3. Khi `D0` đã cung cấp đủ $M + 1$ đỉnh nhỏ, code cũ vẫn rút mù thêm $M + 1$ đỉnh từ `D1` rồi đẩy ngược `rem1_buf_` vào `D1SkipList` (gây tách khối lặp lại).
  4. `D1SkipList::drain_valid_up_to` cấp phát động `std::vector<BlockNode*>` cục bộ ở mỗi lần gọi và kích hoạt thu hồi EBR (quét 512 khe với hàng rào `seq_cst`) ngay cả khi chỉ gỡ 1 khối đơn lẻ.
- **Cách tối ưu:**
  1. **Bảng băm phẳng đánh dấu `epoch` (`dedup_table_`, `dedup_epoch_`):** Khử trùng đỉnh trực tiếp trong $\mathcal{O}(1)$ trên bộ nhớ đệm L1 (xóa bảng ở lượt `pull()` mới chỉ tốn phép tăng `++dedup_epoch_`), loại bỏ hoàn toàn cả 3 lượt `std::sort` theo ID đỉnh.
  2. **Chọn tuyến tính `std::nth_element`:** Thay full `std::sort` trên `merged_buf_` bằng `std::nth_element` để tách $M$ phần tử nhỏ nhất và xác định biên $B_i$ trong thời gian tuyến tính $\mathcal{O}(|\text{merged}|)$.
  3. **Cận cắt sớm `dist_limit` trên `D1SkipList`:** Nếu `D0` (vốn đã có thứ tự tăng dần) đã cung cấp đủ $M + 1$ đỉnh hợp lệ phân biệt với khoảng cách lớn nhất `d0_max_seen`, `D1` chỉ trích xuất các phần tử $\le \text{d0\_max\_seen}$ và dừng ngay khi gặp khối có `upper_bound > d0_max_seen` (giữ nguyên tại chỗ các phần tử lớn hơn trong khối mà không gỡ node).
  4. **Con trỏ đầu `head` + Pool tái chế khối trong `D0Deque` & lưu phần dư tại đầu `D0`:** Thêm trường chỉ số `head` vào `D0Block` để tiêu thụ phần tử theo bước trượt $\mathcal{O}(1)$ không sao chép, tái chế `D0Block` qua `free_blocks_` (0 `malloc`/`free`), và giữ phần dư đã sắp xếp của `pull()` ở ngay đầu `D0` thay vì tái chèn vào `D1SkipList`.
  5. **Gom lô EBR (`retired_staging_`) & Bitmask Shard (`active_shard_mask_`):** Gom đủ 64 khối đã gỡ mới thực hiện 1 lần tăng `global_epoch` + quét khe EBR (giảm 98% hàng rào `seq_cst`); dùng bitmask 64-bit `active_shard_mask_` để khi `init()`/`clear()` chỉ khóa các `PoolShard` thực sự đã cấp phát node.
- **Chứng minh bảo toàn thuật toán:**
  - Việc dùng `std::nth_element` $\mathcal{O}(M)$ thay cho sắp xếp toàn bộ $\mathcal{O}(M \log M)$ bám sát chính xác thiết kế cốt lõi của Lemma 3.3 trong bài báo gốc (tập $S_i$ truyền xuống lời gọi đệ quy con là tập hợp không yêu cầu thứ tự nội bộ).
  - Việc giữ phần dư tại đầu `D0` bảo toàn bất biến hợp nhất $\mathcal{D}_0 \cup \mathcal{D}_1$ vì mọi thao tác `pull()`, `empty()`, `clear()` đều xét đồng thời cả hai tầng dưới cùng khóa `pull_mtx_`.
- **Hiệu quả đo thực tế:** Thời gian pha `D.Pull` ở quy mô 1M đỉnh giảm hơn **3 lần** (`603,96 → 198,03` trên `ER 1M`; `831,09 → 288,51` trên `Scale-Free 1M`).

---

### Hạng mục 3: Loại bỏ lệnh khóa phần cứng `lock cmpxchg16b` ở các pha chỉ đọc

- **Tệp liên quan:** [types.hpp](file:///c:/ttss/bmssp_parallel/include/core/types.hpp), [find_pivots.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/find_pivots.cpp), [bmssp.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/bmssp.cpp)
- **Vấn đề trong code cũ (`2cd0b3c`):**
  - Hàm `AtomicVertexState::load()` thực hiện đọc nguyên tử 128-bit bằng lệnh `lock cmpxchg16b` (so sánh-đổi với chính nó), ép dòng cache sang trạng thái độc quyền (Modified/Exclusive) và khóa đường truyền cache.
  - Code cũ đã có `load_dist()` 64-bit nhẹ cho khoảng cách, nhưng tại bước dựng rừng tuần tự sau barrier (`find_pivots.cpp`) và bước chép mảng kết quả cuối cùng (`bmssp.cpp`, $1–2$ triệu đỉnh, khi mọi luồng song song đã kết thúc), code cũ vẫn gọi `states[v].load()`, phát sinh hàng triệu lệnh `lock cmpxchg16b` tuần tự không cần thiết.
- **Cách tối ưu:**
  - Giữ nguyên 100% lệnh 128-bit CAS `lock cmpxchg16b` trong `relax_edge_atomic()` khi nhiều luồng cập nhật đồng thời `(dist, pred, epoch)` để chống hiện tượng rách dữ liệu (*torn write*).
  - Bổ sung phương thức `AtomicVertexState::load_pred()` (đọc nguyên tử 32-bit chuẩn `__atomic_load_n` tại offset 8 của `pred`, không phát sinh tiền tố `lock`) và `init_raw()` (ghi trực tiếp khi khởi tạo độc quyền trước vùng song song).
- **Chứng minh bảo toàn thuật toán:** Giá trị `dist` và `pred` thu được trùng khớp từng bit 100%, không ảnh hưởng đến tính nguyên tử trong pha relax cạnh song song.

---

### Hạng mục 4: Triệt tiêu nghẽn Spinlock SMT ở 12 luồng và hiệu ứng đoàn tàu khóa (Lockstep Convoy)

- **Tệp liên quan:** [d1_skiplist.hpp](file:///c:/ttss/bmssp_parallel/include/data_structure/d1_skiplist.hpp), [bmssp.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/bmssp.cpp)
- **Vấn đề trong code cũ (`2cd0b3c`):**
  1. Trên CPU 6 nhân vật lý / 12 luồng logic (SMT/Hyper-Threading), mỗi nhân vật lý gánh 2 luồng OpenMP. Vòng lặp `BlockSpinlock::lock()` cũ quay vô hạn (`while (flag.test()) cpu_pause();`). Khi luồng giữ khóa bị hệ điều hành Windows tạm dừng hoặc chia sẻ nhân với luồng anh em đang quay spin, hệ thống mất trọn một lượng tử lập lịch (~15,6 ms), làm tốc độ ở 12 luồng vọt cao bất thường và chậm hơn 8 luồng.
  2. Trong `flush_local_d_to_frontier`, cả 12 luồng sau khi sắp xếp `local_d` tăng dần đều bắt đầu chèn vào `D1SkipList` từ phần tử chỉ số `0` (khoảng cách nhỏ nhất) cùng một thời điểm, tạo hiệu ứng đoàn tàu 12 luồng tranh chấp liên tiếp trên cùng một block.
- **Cách tối ưu:**
  1. Trong `BlockSpinlock::lock()`: giới hạn quay tối đa `64` vòng `cpu_pause()`, sau đó gọi `std::this_thread::yield()` để nhường ngay nhân vật lý cho luồng đang giữ khóa hoàn tất công việc.
  2. Trong `flush_local_d_to_frontier()`: mỗi luồng `tid` chia `local_d` (vốn đã sắp xếp tăng dần) tại vị trí lệch pha `offset = (local_d.size() * tid) / nthreads`, chèn nửa `[offset .. end)` trước rồi đến nửa `[0 .. offset)`. Cả hai nửa đều giữ nguyên thứ tự tăng dần cho `insert_batch_sorted`, nhưng phân tán đều 12 luồng sang các vùng khoảng cách khác nhau của `D1SkipList`.
  3. Điều chỉnh ngưỡng cắt song song theo số luồng hoạt động `max(2048, 512 * max_threads)` để tránh đánh thức 12 luồng SMT cho các lát chỉ có vài nghìn cạnh.
- **Chứng minh bảo toàn thuật toán:** Phép hợp của hai đoạn con `[offset .. end)` và `[0 .. offset)` chứa chính xác cùng tập phần tử `local_d`; thứ tự chèn giữa các luồng đồng thời vào $\mathcal{D}_1$ vốn độc lập theo thiết kế của Lemma 3.3.

---

### Hạng mục 5: Cắt giảm cấp phát tuần tự ~69–138 MB, nén bitmask tầng đệ quy & Khởi tạo *Parallel First-Touch*

- **Tệp liên quan:** [bmssp.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/bmssp.cpp), [find_pivots.hpp](file:///c:/ttss/bmssp_parallel/include/algorithm/find_pivots.hpp), [find_pivots.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/find_pivots.cpp)
- **Vấn đề trong code cũ (`2cd0b3c`):**
  - Mỗi lần gọi `bmssp_parallel()` trên đồ thị $1\text{M} – 2\text{M}$ đỉnh, luồng chính (Thread 0) phải cấp phát mới và chạm trang bộ nhớ (*page fault* tuần tự qua `VirtualAlloc` của Windows) cho khoảng **69 MB (ở 1M) đến 138 MB (ở 2M)** gồm: `atomic_states`, `deduplicator`, `(max_level + 1)` bộ `EpochDeduplicator` riêng cho từng tầng đệ quy (`local_complete_dedups`), 2 bộ `EpochDeduplicator` và 3 mảng `uint32_t` điền sẵn trong `FindPivotsWorkspace`.
- **Cách tối ưu:**
  1. **Nén mảng đánh dấu hoàn thành theo tầng (`level_complete_mask`):** Vì tại mỗi thời điểm trên ngăn xếp đệ quy chỉ có tối đa 1 lời gọi hoạt động ở mỗi tầng `level`, thay `(max_level + 1)` mảng `std::atomic<uint32_t>` (16–32 MB) bằng **1 mảng `uint8_t` duy nhất** (1–2 MB), trong đó tầng $l \in [0, 7]$ dùng bit `(1 << l)` và xóa sạch các bit đã bật trong $\mathcal{O}(|\text{complete}|)$ ngay trước khi thoát hàm đệ quy (vẫn giữ nhánh dự phòng `overflow_dedups` cho trường hợp lý thuyết $l \ge 8$).
  2. **Tối giản `FindPivotsWorkspace`:** Thay hai bộ `EpochDeduplicator` nguyên tử (8–16 MB, chỉ dùng tuần tự trên luồng chính) bằng 1 mảng cờ `uint8_t vertex_flags` (xóa trong $\mathcal{O}(|\text{vis}|)$ trước khi thoát `find_pivots_parallel`); cấp phát lười không điền giá trị rác (`std::make_unique_for_overwrite<uint32_t[]>`) cho 3 mảng rừng (`parent_in_forest`, `root_of`, `subtree_cnt`); đưa các vector trung gian (`unique_S`, `active`, `next_active`, `gather_offsets`, `path_buf`) vào `FindPivotsWorkspace` để tái sử dụng dung lượng bộ nhớ.
  3. **Khởi tạo *Parallel First-Touch* & thu hoạch kết quả song song:** Dùng `#pragma omp parallel for schedule(static)` để khởi tạo song song `atomic_states`, `global_completed`, `level_complete_mask` và chép song song kết quả cuối sang `SSSPResult`, tận dụng băng thông RAM của toàn bộ 6 nhân / 12 luồng.
- **Chứng minh bảo toàn thuật toán:** Mọi cờ bit đều được bật và dọn sạch đối xứng theo đúng phạm vi sống của từng lời gọi `find_pivots_parallel` và `bmssp_rec`, bảo toàn 100% tập đỉnh trả về.

---

### Hạng mục 6: Khử cấp phát bảng băm trong `BaseCase`, bỏ kiểm tra `!D->empty()` thừa và sort lô `K` bằng OpenMP

- **Tệp liên quan:** [base_case.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/base_case.cpp), [bmssp.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/bmssp.cpp)
- **Vấn đề trong code cũ (`2cd0b3c`):**
  1. Hàm `base_case` khởi tạo và hủy mới một `std::unordered_set<uint32_t> settled` cùng vector của `std::priority_queue` ở **mỗi lần gọi** (~400.000 lần trên đồ thị 1M đỉnh), dù với mọi kích thước đồ thị thực tế ($n \le 2^{64}$), tham số $k = \lfloor (\log_2 n)^{1/3} \rfloor \le 4$, tức tập `complete` chỉ chứa tối đa **5 phần tử**!
  2. Điều kiện vòng lặp `while (... && !D->empty())` gọi `D->empty()` (phải khóa `pull_mtx_` + tạo `ReaderGuard` `seq_cst` + khóa `spinlock` block đầu) ở đầu mỗi vòng lặp ngay trước khi gọi `D->pull()`.
  3. Hàm `parallel_sample_sort(K)` tạo và hủy mới các luồng `std::thread` của hệ điều hành mỗi khi $|K| \ge 2048$.
- **Cách tối ưu:**
  1. Trong `base_case.cpp`: với $k \le 32$ (luôn đúng trong BMSSP; vẫn giữ `std::unordered_set` dự phòng nếu gọi độc lập với $k > 32$), kiểm tra đỉnh đã duyệt bằng phép quét tuyến tính trực tiếp trên vector `complete` ($\le 5$ số nguyên 32-bit nằm gọn trong 1 dòng cache L1, tốn ~2 ns và **0 cấp phát bộ nhớ động**), đồng thời tái sử dụng bộ đệm heap `thread_local`.
  2. Trong `bmssp.cpp`: bỏ lời gọi `!D->empty()` thừa trong điều kiện `while` (dùng trực tiếp kết quả trả về `pulled` của `D->pull()` và cờ `d_exhausted`); khi không biên dịch với ThreadSanitizer, sắp xếp lô $K$ lớn trực tiếp trên nhóm luồng OpenMP sẵn có (`sort_k_openmp`: chia đoạn sort song song + trộn nhị phân song song `std::inplace_merge`).
- **Chứng minh bảo toàn thuật toán:** Tập đỉnh đã xét trong `BaseCase`, điều kiện dừng vòng lặp `while` và thứ tự không giảm của lô $K$ trước `batch_prepend` tương đương tuyệt đối 100%.

---

## 3. Danh Mục Toàn Bộ Các Tệp Mã Nguồn Đã Cập Nhật

| Phân hệ | Tệp mã nguồn | Tóm tắt thay đổi |
|---|---|---|
| **Hạ tầng kiểu nguyên tử** | [types.hpp](file:///c:/ttss/bmssp_parallel/include/core/types.hpp) | Thêm `load_pred()` đọc nguyên tử 32-bit không khóa bus và `init_raw()`. |
| **Cấu trúc $\mathcal{D}_0$** | [d0_deque.hpp](file:///c:/ttss/bmssp_parallel/include/data_structure/d0_deque.hpp), [d0_deque.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/d0_deque.cpp) | Thêm con trỏ đầu `head` trong `D0Block` và pool tái chế khối `free_blocks_`. |
| **Cấu trúc $\mathcal{D}_1$** | [d1_skiplist.hpp](file:///c:/ttss/bmssp_parallel/include/data_structure/d1_skiplist.hpp), [d1_skiplist.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/d1_skiplist.cpp) | Giới hạn spin + `yield()` trong `BlockSpinlock`; gom lô EBR 64 khối; `active_shard_mask_`; cận cắt `dist_limit` khi rút phần tử. |
| **Hợp nhất $\mathcal{D}$ (Lemma 3.3)** | [concurrent_frontier_d.hpp](file:///c:/ttss/bmssp_parallel/include/data_structure/concurrent_frontier_d.hpp), [concurrent_frontier_d.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/concurrent_frontier_d.cpp) | Nhánh chính xác $M = 1$ min-heap; bảng băm phẳng `epoch` $\mathcal{O}(1)$; `std::nth_element`; giữ phần dư ở đầu `D0`. |
| **Algorithm 1 (`FindPivots`)** | [find_pivots.hpp](file:///c:/ttss/bmssp_parallel/include/algorithm/find_pivots.hpp), [find_pivots.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/find_pivots.cpp) | Mảng cờ `uint8_t vertex_flags`; mảng rừng `make_unique_for_overwrite`; tái sử dụng buffer workspace; đọc `load_pred()`. |
| **Algorithm 2 (`BaseCase`)** | [base_case.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/base_case.cpp) | Quét trực tiếp `complete` cho $k \le 32$ (bỏ cấp phát `std::unordered_set`) + heap `thread_local`. |
| **Algorithm 3 (`BMSSP`)** | [bmssp.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/bmssp.cpp) | Bitmask `level_complete_mask`; khởi tạo/thu hoạch song song; dịch pha chèn `local_d`; bỏ `!D->empty()` lặp; `sort_k_openmp`. |
| **Build & Benchmark** | [CMakeLists.txt](file:///c:/ttss/bmssp_parallel/CMakeLists.txt), [bench_final_vs_dijkstra.cpp](file:///c:/ttss/bmssp_parallel/benchmarks/bench_final_vs_dijkstra.cpp), [plot_final_vs_dijkstra.py](file:///c:/ttss/bmssp_parallel/benchmarks/plot_final_vs_dijkstra.py) | Đăng ký target đo đối sánh từ 100K đến 2.000.000 đỉnh (1–12 luồng) và sinh biểu đồ PNG. |

---

## 4. Hiệu Quả Thực Nghiệm Đạt Được (Kiểm Chứng 100% Khớp Dijkstra Oracle)

### 4.1. Kết quả kiểm thử tính đúng đắn
- **4/4 bộ kiểm thử đơn vị, biên và tải cao** (`build_and_test.py` & `CTest`): **PASS 100%** (`test_person1`: 153.932 checks; `test_d_stress`: 1.406.792 checks & stress 32 luồng / 10.000.000 thao tác đồng thời; `verify_parallel_bmssp`: 24/24; `test_parallel_edge_cases`: 240 lượt stress chu trình 0, near-tie, đỉnh cô lập, hub lớn, gadget transform).
- **66/66 cấu hình benchmark (11 đồ thị × 6 chế độ):** Khớp 100% khoảng cách với `dijkstra_sequential` trên mọi đỉnh (`MaxDiff = 0.0`).

### 4.2. So sánh trực tiếp Trước tối ưu (`2cd0b3c`) vs Sau tối ưu vs Dijkstra

| Đồ thị (`seed = 20261122`) | Đỉnh ($n$) | Cung ($m$) | `Dijkstra` (1T) | `BMSSP` gốc repo (`2cd0b3c`, 12T) | **`BMSSP` sau tối ưu (8T)** | **`BMSSP` sau tối ưu (12T)** | Nhanh hơn bản cũ (ở 12T) | **Nhanh hơn Dijkstra** |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| **`Dense-16 500K`** | 500.000 | 7.999.081 | 675,38 ms | 722,71 ms | **398,62 ms** | 414,82 ms | `1,74×` | **`1,69×`** |
| **`Erdős–Rényi 1M`** | 1.000.000 | 4.000.740 | 746,77 ms | 885,07 ms | 500,85 ms | **498,66 ms** | `1,77×` | **`1,50×`** |
| **`Scale-Free 1M`** | 1.000.000 | 3.999.990 | 732,21 ms | 1.084,33 ms | **517,58 ms** | 522,90 ms | `2,07×` | **`1,41×`** |
| **`Dense-12 1M`** | 1.000.000 | 11.997.707 | 1.211,13 ms | *(chưa đo ở bản cũ)* | 665,62 ms | **664,68 ms** | — | **`1,82×`** |
| **`Erdős–Rényi 2M`** | 2.000.000 | 7.999.081 | 1.642,68 ms | *(chưa đo ở bản cũ)* | **996,52 ms** | 1.011,66 ms | — | **`1,65×`** |
| **`Scale-Free 2M`** | 2.000.000 | 7.999.990 | 1.685,27 ms | *(chưa đo ở bản cũ)* | **1.043,25 ms** | 1.057,74 ms | — | **`1,62×`** |
| **`Dense-12 2M`** | 2.000.000 | 24.002.527 | 2.645,71 ms | *(chưa đo ở bản cũ)* | 1.330,31 ms | **1.202,72 ms** | — | **`2,20×`** |

*(Bảng đầy đủ 11 đồ thị từ `100K` đến `2M` đỉnh ở tất cả các mức `1, 2, 4, 8, 12` luồng xem tại [FINAL_VS_DIJKSTRA_1M_2M_REPORT.md](file:///c:/ttss/bmssp_parallel/FINAL_VS_DIJKSTRA_1M_2M_REPORT.md) và [final_benchmark_1m_2m.csv](file:///c:/ttss/bmssp_parallel/final_benchmark_1m_2m.csv).)*

---

## 5. Giới Hạn Trung Thực Khi Trình Bày Học Thuật

1. **Nguồn gốc của lợi thế tốc độ thực tế:** Việc Parallel BMSSP chạy nhanh hơn `Dijkstra 1T` từ `1,41×` đến `2,20×` ở quy mô `200K – 2M` đỉnh đạt được nhờ **khai thác tính song song dữ liệu đa nhân (OpenMP 4–12 luồng)** trên các lát khoảng cách của BMSSP kết hợp với việc triệt tiêu overhead cài đặt của cấu trúc $\mathcal{D}$; ở chế độ đơn luồng (`BMSSP 1T`), `Dijkstra 1T` vẫn nhanh hơn nhẹ khoảng `3% – 26%` do cấu trúc heap nhị phân đơn giản và thân thiện bộ nhớ đệm hơn.
2. **Chế độ biểu diễn đồ thị:** Các phép đo hiệu năng sử dụng chế độ chạy trực tiếp trên `CSRGraph` (`use_gadget_transform = false`). Chế độ biến đổi bậc hằng số $d \le 2$ (`use_gadget_transform = true`) vẫn hoạt động chính xác 100% qua các bài kiểm thử nhưng không bật mặc định khi benchmark đồ thị thực tế vì làm tăng số đỉnh/cạnh.
3. **Phạm vi kiểm chứng:** Việc vượt qua 100% bộ kiểm thử đơn vị, kiểm thử biên, stress đa luồng và đối chứng Oracle trên tập đồ thị đến 2 triệu đỉnh là bằng chứng thực nghiệm mạnh mẽ cho bản cài đặt, không thay thế chứng minh toán học hình thức tổng quát cho mọi đầu vào bất kỳ.
