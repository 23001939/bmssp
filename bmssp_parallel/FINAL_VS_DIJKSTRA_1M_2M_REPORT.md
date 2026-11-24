# Báo Cáo Cuối Cùng: Tối Ưu Hóa Parallel BMSSP & Đối Sánh Toàn Diện Với Dijkstra (Quy Mô Tới 2.000.000 Đỉnh, 12 Luồng)

- **Ngày thực hiện:** 24/11/2026 (Asia/Saigon)
- **Nhánh đối chiếu ban đầu:** `origin/parallel-bmssp` commit `2cd0b3c` (Trung — tối ưu gom buffer `FindPivots`) và kịch bản đo 1M của `origin/feat/person4-qa` commit `c76a215` (Dương Văn Tài — seed `20261122`).
- **Môi trường phần cứng & biên dịch:**
  - **CPU:** AMD Ryzen 5 7640HS (6 nhân vật lý / **12 luồng xử lý logic SMT**).
  - **Hệ điều hành:** Windows / PowerShell.
  - **Trình biên dịch:** GCC `16.2.0` (`-std=c++20 -O3 -march=native -mcx16 -Wall -Wextra -fopenmp`), `omp_set_dynamic(0)`.
- **Dữ liệu xuất & Biểu đồ:**
  - Bảng số liệu thô: [final_benchmark_1m_2m.csv](file:///c:/ttss/bmssp_parallel/final_benchmark_1m_2m.csv)
  - Biểu đồ tổng kết: [final_vs_dijkstra_1m_2m.png](file:///c:/ttss/bmssp_parallel/final_vs_dijkstra_1m_2m.png)
  - Mã nguồn benchmark: [bench_final_vs_dijkstra.cpp](file:///c:/ttss/bmssp_parallel/benchmarks/bench_final_vs_dijkstra.cpp), [plot_final_vs_dijkstra.py](file:///c:/ttss/bmssp_parallel/benchmarks/plot_final_vs_dijkstra.py)

---

## 1. Tóm Tắt Kết Quả Đạt Được

1. **Độ chính xác tuyệt đối 100%:**
   - Cả **4/4 bộ kiểm thử tự động** (`test_person1`: 153.932 checks; `test_d_stress`: 1.406.792 checks & stress 32 luồng / 10.000.000 thao tác; `verify_parallel_bmssp`: 24/24 ca; `test_parallel_edge_cases`: 6/6 suite & 240 lượt stress) và **66/66 tổ hợp benchmark** từ `100.000` đến `2.000.000` đỉnh (`24.002.527` cung có hướng) đều khớp 100% khoảng cách với `dijkstra_sequential` Oracle độc lập (`MaxDiff = 0.0`).
2. **Đảo chiều tốc độ thực tế so với Dijkstra:**
   - Trên bản mới nhất vừa kéo từ repo (`2cd0b3c`), ở quy mô 1 triệu đỉnh, Parallel BMSSP vẫn chậm hơn Dijkstra (`869,15 – 885,07 ms` vs `834,34 ms` trên `ER 1M`; `1.078,66 – 1.084,33 ms` vs `812,23 ms` trên `Scale-Free 1M`).
   - Sau gói **6 tối ưu hệ thống & cấu trúc dữ liệu** (bảo toàn 100% thuật toán Duan et al. STOC 2025, Algorithm 1–3 và Lemma 3.3, không cắt giảm tham số hay fallback sang Dijkstra), **Parallel BMSSP vượt qua Dijkstra bắt đầu từ mốc `200.000` đỉnh và mở rộng cách biệt mạnh mẽ ở `1.000.000` (`1M`) và `2.000.000` (`2M`) đỉnh**:
     - **`Erdős–Rényi 1M` (`m = 4M`):** giảm từ `885,07 ms` xuống **`498,66 ms` (12T)** so với Dijkstra `746,77 ms` (**nhanh hơn `1,50×`**, tiết kiệm `33,2%` thời gian).
     - **`Scale-Free 1M` (`m = 4M`, bậc đỉnh hub `62.550`):** giảm từ `1.084,33 ms` xuống **`517,58 ms` (8T) / `522,90 ms` (12T)** so với Dijkstra `732,21 ms` (**nhanh hơn `1,41×`**, nhanh gần gấp đôi bản trước tối ưu).
     - **`Dense-12 Random 1M` (`m = 12M`):** đạt **`664,68 ms` (12T)** so với Dijkstra `1.211,13 ms` (**nhanh hơn `1,82×`**).
     - **`Erdős–Rényi 2M` (`n = 2M, m = 8M`):** đạt **`996,52 ms` (8T) / `1.011,66 ms` (12T)** so với Dijkstra `1.642,68 ms` (**nhanh hơn `1,65×`**, phá mốc dưới 1 giây).
     - **`Scale-Free 2M` (`n = 2M, m = 8M`, bậc đỉnh hub `107.839`):** đạt **`1.043,25 ms` (8T) / `1.057,74 ms` (12T)** so với Dijkstra `1.685,27 ms` (**nhanh hơn `1,62×`**).
     - **`Dense-12 Random 2M` (`n = 2M, m = 24M`):** đạt **`1.202,72 ms` (12T)** so với Dijkstra `2.645,71 ms` (**nhanh hơn `2,20×`**, giảm `54,5%` thời gian chạy; tự tăng tốc `2,28×` so với BMSSP 1T).

---

## 2. Chẩn Đoán Điểm Nghẽn Trên Bản `2cd0b3c` & Các Tối Ưu Đã Triển Khai

Phân rã thời gian từng pha ở quy mô `1.000.000` đỉnh / `12` luồng trên bản `2cd0b3c` chỉ ra **5 điểm nghẽn kỹ thuật nghiêm trọng**:

1. **Thoái hóa khối đơn vị (`M = 1`) và 4 lần `std::sort` mỗi lượt `D.Pull` (chiếm `50% – 57%` tổng thời gian, `~455 – 623 ms` ở 1M đỉnh):**
   - Ở tầng đệ quy `level = 1`, tham số kích thước khối của Lemma 3.3 là $M = 2^{(1-1)t} = 1$. Việc đẩy hàng trăm nghìn đỉnh qua `D1SkipList` với dung lượng khối `M = 1` gây chia tách node (`handle_overflow_locked` dưới `tower_mtx_`), gỡ liên kết (`unlink_node_locked`) và quét 512 khe EBR (`seq_cst` fence) ở **mọi đỉnh đơn lẻ**.
   - Ngoài ra, ở các tầng `M > 1`, `ConcurrentFrontierD::pull()` thực hiện tới 3 lượt `std::sort` chỉ để khử trùng đỉnh, 1 lượt full `std::sort` theo khoảng cách, rồi tái chèn phần dư vào `D1SkipList` gây chia tách lặp lại.
   - **Giải pháp ([concurrent_frontier_d.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/concurrent_frontier_d.cpp), [d0_deque.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/d0_deque.cpp), [d1_skiplist.cpp](file:///c:/ttss/bmssp_parallel/src/data_structure/d1_skiplist.cpp)):**
     - Chuyên biệt hóa trường hợp biên chính xác `M = 1` bên trong `ConcurrentFrontierD` bằng hàng đợi ưu tiên nhị phân phẳng bảo vệ bởi `BlockSpinlock` (bảo toàn 100% ngữ nghĩa `IFrontierD`, lọc stale, gom trùng đỉnh và thống kê QA).
     - Với `M > 1`: thay 3 lượt `std::sort` khử trùng bằng bảng băm phẳng đánh dấu `epoch` $\mathcal{O}(1)$ trong L1 cache; dùng chọn tuyến tính `std::nth_element` $\mathcal{O}(|merged|)$ thay cho full sort; thêm cận cắt `dist_limit` khi `D0` đã đủ `M + 1` phần tử; tái chế khối `D0Block` bằng con trỏ đầu `head` (0 `malloc`/`free`); giữ phần dư đã sắp xếp ở đầu `D0` thay vì tái chèn gây split lại `D1`; và gom lô thu hồi EBR theo ngưỡng 64 khối.
   - **Hiệu quả đo thực tế:** Thời gian pha `D.Pull` ở 1M đỉnh giảm hơn **3×** (`604 → 198 ms` trên `ER 1M`; `831 → 288 ms` trên `Scale-Free 1M`).

2. **Tranh chấp Spinlock SMT ở 12 luồng và hiệu ứng đoàn tàu (Lockstep Convoy):**
   - Trên CPU 6 nhân vật lý / 12 luồng SMT, vòng lặp spin vô hạn trong `BlockSpinlock::lock()` khiến luồng anh em cùng nhân chiếm dụng chu kỳ thực thi khi luồng giữ khóa bị hệ điều hành lập lịch xen ngang (gây vọt thời gian ở 12T). Đồng thời 12 luồng cùng chèn `local_d` từ khoảng cách nhỏ đến lớn tạo đoàn tàu khóa tại cùng một block đầu của `D1SkipList`.
   - **Giải pháp ([d1_skiplist.hpp](file:///c:/ttss/bmssp_parallel/include/data_structure/d1_skiplist.hpp), [bmssp.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/bmssp.cpp)):** Bổ sung giới hạn spin 64 vòng kèm `std::this_thread::yield()` trong `BlockSpinlock`, và dịch vòng điểm bắt đầu chèn `(local_d.size() * tid) / nthreads` để phân tán 12 luồng sang các block khác nhau.

3. **Cấp phát tuần tự ~69–138 MB mỗi lần gọi & `1–2 triệu` lệnh `lock cmpxchg16b` tuần tự khi đọc kết quả:**
   - `BMSSPContext` và `FindPivotsWorkspace` từng cấp phát và điền tuần tự trên luồng 0 nhiều mảng `std::atomic<uint32_t>` / `uint32_t` kích thước $n$, sau đó đọc kết quả bằng `atomic_states[i].load()` (phát sinh lệnh `lock cmpxchg16b` tuần tự cho mọi đỉnh).
   - **Giải pháp ([types.hpp](file:///c:/ttss/bmssp_parallel/include/core/types.hpp), [find_pivots.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/find_pivots.cpp), [bmssp.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/bmssp.cpp)):**
     - Bổ sung `AtomicVertexState::load_pred()` (đọc nguyên tử 32-bit nhẹ, loại bỏ hoàn toàn `lock cmpxchg16b` khi dựng rừng `FindPivots` và khi xuất `SSSPResult`).
     - Gộp `(max_level + 1)` bộ `EpochDeduplicator` thành một mảng bitmask `uint8_t` duy nhất (`level_complete_mask`, xóa $\mathcal{O}(|complete|)$ khi thoát đệ quy); dùng cờ `uint8_t` và `std::make_unique_for_overwrite` cho mảng rừng trong `FindPivotsWorkspace`.
     - Khởi tạo song song (`#pragma omp parallel for schedule(static)`) cho `atomic_states` (tối ưu *parallel first-touch*) và thu hoạch kết quả `dist`/`pred` song song ở cuối thuật toán.

4. **Loại bỏ cấp phát bảng băm trong `BaseCase` & tránh tạo luồng OS khi sắp xếp `K`:**
   - Trong [base_case.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/base_case.cpp): vì $k = \lfloor \log_2^{1/3} n \rfloor \le 4$ với mọi đồ thị 64-bit, thay `std::unordered_set<uint32_t>` (cấp phát hàng trăm nghìn lần mỗi lượt giải) bằng quét mảng tĩnh `complete` trong thanh ghi kết hợp buffer heap `thread_local`.
   - Trong [bmssp.cpp](file:///c:/ttss/bmssp_parallel/src/algorithm/bmssp.cpp): sắp xếp lô `K` trực tiếp trên nhóm luồng OpenMP sẵn có (`sort_k_openmp`) thay vì tạo/hủy `std::thread` của hệ điều hành, và bỏ lời gọi khóa thừa `!D->empty()` ở đầu mỗi vòng lặp.

---

## 3. Đối Chiếu Trước Tối Ưu (`2cd0b3c`) vs Sau Tối Ưu (1M Đỉnh, Seed `20261122`)

*(Đo trên cùng máy Ryzen 5 7640HS, cùng trình biên dịch GCC 16.2.0 `-O3 -march=native -mcx16 -fopenmp`, cùng đồ thị và nguồn `0`.)*

| Đồ thị | `Dijkstra (1T)` | `BMSSP (2cd0b3c)` 8T | `BMSSP (2cd0b3c)` 12T | **`BMSSP (Tối ưu)` 8T** | **`BMSSP (Tối ưu)` 12T** | Cải thiện tại 12T | Vị thế so với Dijkstra |
|---|---:|---:|---:|---:|---:|---:|---|
| **`Erdős–Rényi 1M`** (`deg=4, m=4M`) | ~747 – 834 ms | 869,15 ms | 885,07 ms | **500,85 ms** | **498,66 ms** | **Nhanh hơn `1,77×` (`-43,7%`)** | Chậm hơn `1,04×` → **Nhanh hơn `1,50×`** |
| **`Scale-Free 1M`** (`deg=4, m=4M`) | ~732 – 812 ms | 1.078,66 ms | 1.084,33 ms | **517,58 ms** | **522,90 ms** | **Nhanh hơn `2,07×` (`-51,8%`)** | Chậm hơn `1,33×` → **Nhanh hơn `1,41×`** |
| **`Dense-16 500K`** (`deg=16, m=8M`) | ~661 – 675 ms | 677,05 ms | 722,71 ms | **398,62 ms** | **414,82 ms** | **Nhanh hơn `1,74×` (`-42,6%`)** | Chậm hơn `1,02×` → **Nhanh hơn `1,69×`** |

---

## 4. Bảng Kết Quả Đối Sánh Toàn Diện Từ 100.000 Đến 2.000.000 Đỉnh (1 – 12 Luồng)

### Phương pháp đo
- Tất cả phép đo chạy **tuần tự từng cấu hình** trên cùng đồ thị `CSRGraph` đã nạp trong RAM (không tính thời gian sinh đồ thị, đọc/ghi file; cùng cờ biên dịch tối ưu `-O3 -march=native -mcx16`, tắt profiling).
- Mỗi điểm dữ liệu thực hiện `1–2` lượt làm nóng (warmup) + `3–5` mẫu đo độc lập, lấy **trung vị (`Median ms`)**.
- **Mọi lượt chạy** (cả warmup lẫn mẫu đo) đều kiểm tra đối chiếu toàn bộ $n$ khoảng cách với `dijkstra_sequential` (`tol = 1e-9`). Kết quả thực tế: **`MaxDiff = 0.0000`** trên toàn bộ 11 đồ thị (tất cả trọng số nguyên được tính chính xác tuyệt đối).

### Bảng tổng hợp thời gian trung vị (`ms`)

| Mã | Đồ thị (`seed = 20261122`) | Đỉnh ($n$) | Cung ($m$) | Bậc ra max | `Dijkstra` (1T) | `BMSSP` 1T | `BMSSP` 2T | `BMSSP` 4T | `BMSSP` 8T | `BMSSP` 12T | **Tốt nhất (Par)** | **Nhanh hơn Dijkstra** | **Tự tăng tốc (`1T/Par`)** | Kiểm chứng |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|:---:|
| `ER_100K` | Erdős–Rényi 100K (`deg=4`) | 100.000 | 400.713 | 15 | **48,45** | 72,57 | 65,72 | 53,95 | 55,56 | 58,51 | 53,95 | `0,90×` | `1,35×` | `OK` |
| `ER_200K` | Erdős–Rényi 200K (`deg=4`) | 200.000 | 801.243 | 15 | 117,19 | 174,82 | 136,68 | 113,00 | **106,75** | 110,27 | **106,75** | **`1,10×`** | `1,64×` | `OK` |
| `D20_200K` | Dense-20 Random 200K (`deg=20`) | 200.000 | 4.000.741 | 42 | 254,12 | 300,91 | 244,00 | 184,86 | **163,48** | 177,86 | **163,48** | **`1,55×`** | `1,84×` | `OK` |
| `ER_500K` | Erdős–Rényi 500K (`deg=4`) | 500.000 | 2.000.349 | 17 | 366,30 | 464,15 | 363,04 | 291,12 | **263,99** | 271,69 | **263,99** | **`1,39×`** | `1,76×` | `OK` |
| `D16_500K` | Dense-16 Random 500K (`deg=16`) | 500.000 | 7.999.081 | 38 | 675,38 | 813,15 | 639,32 | 494,25 | **398,62** | 414,82 | **398,62** | **`1,69×`** | `2,04×` | `OK` |
| `ER_1M` | Erdős–Rényi 1M (`deg=4`) | **1.000.000** | 4.000.740 | 17 | 746,77 | 896,54 | 705,18 | 554,14 | 500,85 | **498,66** | **498,66** | **`1,50×`** | `1,80×` | `OK` |
| `SF_1M` | Scale-Free 1M (`deg=4`) | **1.000.000** | 3.999.990 | 62.550 | 732,21 | 877,15 | 693,09 | 575,17 | **517,58** | 522,90 | **517,58** | **`1,41×`** | `1,69×` | `OK` |
| `D12_1M` | Dense-12 Random 1M (`deg=12`) | **1.000.000** | 11.997.707 | 33 | 1.211,13 | 1.406,66 | 1.043,20 | 796,17 | 665,62 | **664,68** | **664,68** | **`1,82×`** | `2,12×` | `OK` |
| `ER_2M` | Erdős–Rényi 2M (`deg=4`) | **2.000.000** | 7.999.081 | 17 | 1.642,68 | 1.869,91 | 1.466,39 | 1.119,80 | **996,52** | 1.011,66 | **996,52** | **`1,65×`** | `1,88×` | `OK` |
| `SF_2M` | Scale-Free 2M (`deg=4`) | **2.000.000** | 7.999.990 | 107.839 | 1.685,27 | 1.913,89 | 1.460,71 | 1.185,89 | **1.043,25** | 1.057,74 | **1.043,25** | **`1,62×`** | `1,83×` | `OK` |
| `D12_2M` | Dense-12 Random 2M (`deg=12`) | **2.000.000** | **24.002.527** | 33 | 2.645,71 | 2.736,98 | 2.033,11 | 1.584,91 | 1.330,31 | **1.202,72** | **1.202,72** | **`2,20×`** | `2,28×` | `OK` |

---

## 5. Phân Tích Khoa Học & Giới Hạn Trung Thực

1. **Bản chất lợi thế tốc độ (Song song đa nhân vs Tiệm cận tuần tự):**
   - Ở chế độ **1 luồng đơn lẻ (`BMSSP 1T`)**, thuật toán BMSSP hiện chỉ còn chậm hơn `Dijkstra 1T` khoảng **`1,03× – 1,26×`** ở quy mô `1M – 2M` đỉnh (ví dụ trên `D12_2M`: `2.736,98 ms` so với `2.645,71 ms` — chỉ chênh `3,4%`!). Đây là khoảng cách 1 luồng hẹp nhất từng đạt được trong dự án nhờ cấu trúc CSR trực tiếp và tối ưu `ConcurrentFrontierD`.
   - Khi bật **song song hóa đa luồng (`2T – 12T`)**, cấu trúc cắt lát khoảng cách (Distance-Sliced BMSSP) cho phép hàng trăm nghìn phép relax cạnh trong `FindPivots` và `bmssp_rec` thực thi đồng thời trên 6 nhân / 12 luồng, trong khi hàng đợi nhị phân của Dijkstra tuần tự bị khóa cứng bởi thứ tự trích xuất đỉnh đơn chiếc. Nhờ đó, **ngay từ mức 2 luồng (`BMSSP 2T`) ở quy mô $\ge 500\text{K}$ đỉnh, BMSSP đã bắt đầu nhanh hơn Dijkstra**, và đạt đỉnh ở **8–12 luồng** (nhanh hơn **`1,41× – 2,20×`**).
2. **Điểm giao cắt theo quy mô đồ thị và mật độ cạnh:**
   - Với đồ thị nhỏ (`n <= 100.000`, thưa `deg=4`), dữ liệu nằm gọn trong bộ nhớ đệm L2/L3 và frontier mỗi lát nhỏ nên chi phí đồng bộ luồng khiến Dijkstra 1 luồng vẫn nhỉnh hơn (`48,45 ms` vs `53,95 ms`).
   - Từ **`n >= 200.000`** trở lên — đặc biệt khi tăng lên **`1.000.000`** và **`2.000.000`** đỉnh hoặc mật độ bậc trung bình tăng (`deg = 12..20`) — khối lượng cạnh mỗi lát vượt xa ngưỡng đồng bộ OpenMP và bộ nhớ hàng đợi của Dijkstra vượt khỏi L3 cache (gây cache miss ngẫu nhiên trên heap), giúp Parallel BMSSP tăng khoảng cách thắng lợi từ `1,10×` (`200K`) → `1,50×` (`1M`) → `1,65×` (`2M`, `deg=4`) và lên tới **`2,20×`** (`2M`, `deg=12`).
3. **Hành vi ở 8 luồng vs 12 luồng trên CPU 6 nhân / 12 luồng SMT:**
   - Do CPU thử nghiệm có **6 nhân vật lý** (12 luồng là SMT Hyper-Threading chia sẻ băng thông bộ nhớ trên cùng 6 nhân), trên đồ thị thưa `deg=4` (`bị giới hạn bởi băng thông RAM`), mức 8T và 12T cho tốc độ tương đương nhau (`~500 ms` ở 1M, `~1.000 ms` ở 2M); trong khi trên đồ thị đặc hơn (`Dense-12 2M`, `24M` cạnh — tỷ lệ tính toán/bộ nhớ cao hơn), **12 luồng phát huy thêm lợi thế SMT và đạt mốc nhanh nhất `1.202,72 ms` (so với `1.330,31 ms` ở 8T)**.
4. **Giới hạn phạm vi kết luận:**
   - Các phép đo chạy trực tiếp trên đồ thị CSR (`use_gadget_transform = false`); chế độ biến đổi bậc hằng số $d \le 2$ (`use_gadget_transform = true`) vẫn được bảo toàn và vượt qua toàn bộ test suite nhưng không bật mặc định vì làm tăng kích thước đồ thị thực tế.
   - Kiểm chứng 100% khớp Oracle trên tập thử nghiệm hữu hạn xác nhận tính đúng thực nghiệm cho các ca đã đo, không thay thế chứng minh hình thức tổng quát cho mọi bất biến/cận worst-case lý thuyết.

---

## 6. Lệnh Tái Lập Toàn Bộ Kết Quả

Chạy từ thư mục gốc `C:\ttss` bằng PowerShell:

```powershell
# 1. Biên dịch và chạy toàn bộ 4 bộ kiểm thử tự động
python -X utf8 bmssp_parallel/build_and_test.py

# 2. Biên dịch và chạy bộ benchmark 100K -> 2M đỉnh (1, 2, 4, 8, 12 luồng)
g++ -std=c++20 -O3 -march=native -mcx16 -Wall -Wextra -fopenmp `
  -Ibmssp_parallel/include -Ibmssp_parallel/include/baseline/external `
  -Ibmssp_parallel/include/baseline/upstream/include `
  bmssp_parallel/benchmarks/bench_final_vs_dijkstra.cpp `
  bmssp_parallel/src/core/graph.o bmssp_parallel/src/baseline/bmssp_seq.o `
  bmssp_parallel/src/data_structure/d0_deque.o bmssp_parallel/src/data_structure/d1_skiplist.o `
  bmssp_parallel/src/data_structure/concurrent_frontier_d.o `
  bmssp_parallel/src/algorithm/find_pivots.o bmssp_parallel/src/algorithm/base_case.o `
  bmssp_parallel/src/algorithm/bmssp.o `
  -o bmssp_parallel/benchmarks/bench_final_vs_dijkstra.exe

.\bmssp_parallel\benchmarks\bench_final_vs_dijkstra.exe bmssp_parallel/final_benchmark_1m_2m.csv

# 3. Sinh biểu đồ tổng hợp PNG
python -X utf8 bmssp_parallel/benchmarks/plot_final_vs_dijkstra.py `
  bmssp_parallel/final_benchmark_1m_2m.csv bmssp_parallel/final_vs_dijkstra_1m_2m.png
```
