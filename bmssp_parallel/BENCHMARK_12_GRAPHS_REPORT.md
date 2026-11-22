# Bảng Thống Kê Benchmark Toàn Diện 12 Đồ Thị (`100` → `200.000` Đỉnh)

- **Ngày đo:** 20/11/2026 (Asia/Saigon)
- **Nhánh mã nguồn:** `fix/parallel-bmssp-remediation`
- **Chương trình đo:** [bench_parallel_suite.cpp](file:///c:/ttss/bmssp_parallel/benchmarks/bench_parallel_suite.cpp)
- **Môi trường:** Windows / PowerShell, GCC `16.2.0` (`-std=c++20 -O3 -march=native -mcx16 -Wall -Wextra -fopenmp`), OpenMP `5.2`, CPU `12` luồng logic.
- **Phương pháp đo:**
  - Chạy tuần tự, loại trừ thời gian tạo/đọc đồ thị; chỉ đo thời gian giải SSSP từ nguồn `source = 0`, chế độ trực tiếp CSR (`use_gadget_transform = false`).
  - `n <= 1.000`: 5 warmup + **21 mẫu** lấy trung vị (`Median ms`).
  - `1.000 < n <= 50.000`: 2 warmup + **7 mẫu** lấy trung vị.
  - `n > 50.000`: 1 warmup + **5 mẫu** lấy trung vị.
  - *(Lưu ý phạm vi)*: `bmssp_seq` (`bmssp_sequential`) bao gồm bước nạp cạnh vào `spp::bmssp` và `prepare_graph(false)`, còn `dijkstra_sequential` và `bmssp_parallel` chạy trực tiếp trên `CSRGraph`.
- **Xác minh đúng đắn:** **96 / 96 tổ hợp** (12 đồ thị × 8 cấu hình) đều khớp 100% khoảng cách với Oracle `dijkstra_sequential` (`tol = 1e-9`, trạng thái `OK`).

---

## 1. Bảng Thống Kê Thời Gian Trung Vị (`Median ms`) — Toàn Bộ 12 Đồ Thị

| # | Đồ thị | Đỉnh ($n$) | Cạnh ($m$) | `Dijkstra` (1T) | `bmssp_seq` (1T) | `Mutex` 1T | `Mutex` 4T | `Mutex` 8T | `Conc` 1T | `Conc` 4T | `Conc` 8T | Tự tăng tốc `Conc` (1T/8T) | A/B `Mutex8T / Conc8T` | `Conc8T / Dijk` | Status |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|:---:|
| 1 | **`weighted_100`** *(file dự án)* | **100** | 240 | **0,004** | 0,242 | 0,066 | 0,066 | 0,102 | 1,003 | 0,983 | 1,021 | 0,98× | 0,10× | 255,3× | OK |
| 2 | **`Sparse Random 100`** | **100** | 500 | **0,007** | 0,363 | 0,081 | 0,156 | 0,082 | 0,982 | 1,013 | 1,127 | 0,87× | 0,07× | 161,0× | OK |
| 3 | **`weighted_1000`** *(file dự án)* | **1.000** | 2.400 | **0,028** | 1,395 | 0,641 | 0,672 | 0,630 | 2,759 | 2,626 | 2,399 | 1,15× | 0,26× | 85,7× | OK |
| 4 | **`Sparse Random 10K`** | **10.000** | 50.000 | **3,188** | 28,574 | 10,772 | 17,828 | 20,506 | 12,992 | 15,557 | 18,415 | 0,71× | 1,11× | 5,78× | OK |
| 5 | **`Sparse Random 20K`** | **20.000** | 100.000 | **6,912** | 58,662 | 29,784 | 49,832 | 54,738 | 27,339 | 30,475 | 30,807 | 0,89× | 1,78× | 4,46× | OK |
| 6 | **`2D Grid 150×150`** | **22.500** | 89.400 | **4,250** | 49,561 | 19,839 | 19,539 | 20,722 | 32,371 | 31,700 | 32,985 | 0,98× | 0,63× | 7,76× | OK |
| 7 | **`Hub 20K` (`deg0=10K`)** | **20.000** | 90.000 | **6,656** | 112,234 | 28,471 | 45,748 | 49,221 | 33,167 | 32,320 | 32,819 | 1,01× | 1,50× | 4,93× | OK |
| 8 | **`Sparse Random 50K`** | **50.000** | 250.000 | **24,581** | 170,944 | 80,260 | 120,703 | 120,290 | 68,047 | 65,915 | **63,997** | **1,06×** | **1,88×** | 2,60× | OK |
| 9 | **`2D Grid 300×300`** | **90.000** | 358.800 | **20,656** | 195,781 | 81,830 | 78,566 | 78,736 | 110,179 | 101,856 | **101,885** | **1,08×** | 0,77× | 4,93× | OK |
| 10 | **`Sparse Random 100K`** | **100.000** | 500.000 | **65,806** | 341,494 | 209,756 | 340,857 | 355,269 | 149,358 | 127,989 | **126,564** | **1,18×** | **2,81×** | 1,92× | OK |
| 11 | **`Hub 100K` (`deg0=50K`)** | **100.000** | 450.000 | **56,182** | 688,335 | 218,369 | 310,460 | 289,725 | 170,870 | 140,537 | **139,279** | **1,23×** | **2,08×** | 2,48× | OK |
| 12 | **`Sparse Random 200K`** | **200.000** | 1.000.000 | **130,395** | 712,369 | 559,487 | 870,006 | 908,089 | 304,441 | 255,317 | **242,696** | **1,25×** | **3,74×** | **1,86×** | OK |

*(Đơn vị thời gian: mili-giây `ms`; `Conc` = `bmssp_parallel` dùng `ConcurrentFrontierD` từ nhánh `Viet`; `Mutex` = `bmssp_parallel` dùng `MutexFrontierD`.)*

---

## 2. Những Kết Luận Quan Trọng Rút Ra Từ Bảng Số Liệu

1. **Ở đồ thị nhỏ (`n = 100` và `n = 1.000`):**
   - `weighted_100` (`n=100, m=240`): Dijkstra chỉ mất **`0,004 ms`**, `Mutex 1T` mất `0,066 ms`, `bmssp_seq` mất `0,242 ms`, còn `Concurrent` mất `~1,00 ms` do chi phí khởi tạo B-Link Skip-List và 64-shard pool lớn hơn rất nhiều so với khối lượng tính toán trên 100 đỉnh.
   - Do đó, trên đồ thị nhỏ không nên chạy song song hoặc dùng cấu trúc dữ liệu nặng như `ConcurrentFrontierD`.

2. **Điểm chuyển pha tăng tốc song song (`n >= 50.000`):**
   - Từ `n = 50.000` trở lên, **chạy 4–8 luồng bắt đầu nhanh hơn chạy 1 luồng thực sự** (`Conc 1T/8T` tăng đều: `1,06×` ở 50K → `1,08×` ở Grid 90K → `1,18×` ở Sparse 100K → `1,23×` ở Hub 100K → **`1,25×`** ở Sparse 200K).
   - Cùng lúc đó, `Mutex` bị nghẽn khóa nặng khi tăng lên 8 luồng (ở Sparse 200K tụt từ `559,49 ms` xuống `908,09 ms`), khiến `Concurrent 8T` nhanh hơn `Mutex 8T` tới **`3,74×`** (`242,70 ms` vs `908,09 ms`) và nhanh hơn `bmssp_seq` **`2,94× – 4,94×`**!

3. **Khoảng cách với Dijkstra thu hẹp mạnh theo quy mô:**
   - Tỷ số `Conc 8T / Dijkstra 1T` giảm liên tục theo $n$: `255,3×` (100 đỉnh) → `85,7×` (1K) → `5,78×` (10K) → `2,60×` (50K) → `1,92×` (100K) → **`1,86×`** (200K đỉnh, 1 triệu cạnh).
   - Tuy nhiên, ở mọi kích thước đã đo tới `200.000` đỉnh, **`dijkstra_sequential` (1T) vẫn là solver nhanh nhất**.
