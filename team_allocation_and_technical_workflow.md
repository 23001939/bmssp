# Kế Hoạch Phân Công Kỹ Thuật & Quy Trình Phối Hợp Dự Án Song Song Hóa BMSSP

Tài liệu này quy định chi tiết phạm vi công việc, giao diện kỹ thuật, cấu trúc thư mục, quy trình tích hợp mã nguồn và tiêu chí nghiệm thu định lượng cho nhóm **4 thành viên** triển khai dự án song song hóa thuật toán **BMSSP (Bounded Multi-Source Shortest Path)** trên kiến trúc CPU đa lõi (C++20 / OpenMP / TBB), bám sát các nguyên lý từ bài báo gốc *"Breaking the Sorting Barrier for Directed Single-Source Shortest Paths"* (Duan, Ji, Mao - arXiv:2504.17033v2) và tài liệu thiết kế kỹ thuật [bmssp_parallelization_design_document.md](file:///E:/bmssp/bmssp_parallelization_design_document.md).

---

## 1. Lộ Trình Thực Hiện & Phân Tích Chống Xung Đột (Conflict Prevention)

Dự án được triển khai theo mô hình **Bottom-Up kết hợp Tích hợp cuốn chiếu (Incremental Integration)** theo sơ đồ luồng:

$$\text{Người 1 (Hạ tầng \& Nền tảng)} \longrightarrow \begin{cases} \text{Người 2 (Cấu trúc dữ liệu đồng thời } \mathcal{D} \text{)} \\ \text{Người 3 (Lõi thuật toán BMSSP \& FindPivots)} \end{cases} \longrightarrow \text{Người 4 (Kiểm thử, QA \& Tối ưu)}$$

### Các nguyên tắc vàng triệt tiêu xung đột mã nguồn (Zero Git Merge Conflicts):
1. **Khóa cứng giao diện trừu tượng từ đầu (Interface Contract):** Người 1 định nghĩa toàn bộ kiểu dữ liệu lõi (`VertexState`), biểu diễn đồ thị CSR và interface thuần ảo `IFrontierD`. Người 2 và Người 3 chỉ `#include` các header này ở chế độ chỉ đọc.
2. **Tách biệt không gian tệp vật lý (Physical File Isolation):** Người 2 chỉ phát triển trong thư mục `src/data_structure/`, Người 3 chỉ phát triển trong `src/algorithm/`. Hai nhánh hoàn toàn không chỉnh sửa chung bất kỳ tệp thực thi nào.
3. **Phát triển không chờ đợi chéo (Non-Blocking Development):** Người 1 cung cấp sẵn bản mockup `MutexFrontierD` (dùng `std::mutex` bọc container chuẩn). Nhờ đó, Người 3 có thể biên dịch, chạy thử và kiểm chứng toàn bộ logic thuật toán song song mà không bị phụ thuộc vào tiến độ xây dựng `Concurrent Skip-List` của Người 2.
4. **Thống nhất nguồn sự thật cho Lazy Deletion (Single Source of Truth):** Không để Người 2 quản lý mảng khoảng cách riêng (dễ lệch pha dữ liệu). Người 2 đối chiếu trực tiếp với mảng trạng thái toàn cục `std::atomic<VertexState> state[n]` do Người 1 cung cấp khi thực hiện dọn dẹp rác (Lazy Cleanup) trong hàm `Pull(D)`.

---

## 2. Cấu Trúc Thư Mục Dự Án & Ma Trận Phân Quyền Tệp (File Ownership & RACI)

### 2.1. Cây Thư Mục Dự Án Chuẩn Hóa
```text
bmssp_parallel/
├── CMakeLists.txt                    # [Người 1 khởi tạo khung, Người 4 bổ sung test/bench]
├── README.md                         # [Cả nhóm cập nhật hướng dẫn build và chạy]
│
├── include/                          # Header interfaces & types (Public APIs)
│   ├── core/
│   │   ├── types.hpp                 # [Người 1] VertexState 128-bit atomic, hằng số hệ thống
│   │   ├── graph.hpp                 # [Người 1] CSRGraph, Edge, Gadget Constant-Degree Transform
│   │   ├── memory_pool.hpp           # [Người 1] Thread-local Arena Allocator cho các Block
│   │   ├── frontier_interface.hpp    # [Người 1] IFrontierD, MutexFrontierD (Mockup)
│   │   └── primitives.hpp            # [Người 1] Parallel Prefix Scan, SampleSort, visited_epoch
│   ├── data_structure/
│   │   ├── d0_deque.hpp              # [Người 2] Hàng đợi Block D_0 cho BatchPrepend
│   │   └── d1_skiplist.hpp           # [Người 2] Concurrent Skip-List D_1, Block Spinlocks, Split
│   └── algorithm/
│       ├── find_pivots.hpp           # [Người 3] Relax k-bước song song, phân rã rừng cây F
│       ├── bmssp.hpp                 # [Người 3] Điều phối vòng lặp cắt lát khoảng cách & relax U_i
│       └── base_case.hpp             # [Người 3] Dijkstra tuần tự cho tầng đáy l = 0
│
├── src/                              # Implementation files (Triển khai chi tiết)
│   ├── core/
│   │   └── graph.cpp                 # [Người 1] Đọc file, nạp CSR, Gadget Transform
│   ├── data_structure/
│   │   ├── d0_deque.cpp              # [Người 2] Hiện thực D_0 Block Deque
│   │   └── d1_skiplist.cpp           # [Người 2] Hiện thực D_1 Concurrent Skip-List + Eager Cleanup
│   ├── algorithm/
│   │   ├── find_pivots.cpp           # [Người 3] Lõi FindPivots song song
│   │   ├── bmssp.cpp                 # [Người 3] Lõi BMSSP song song đa tầng
│   │   └── base_case.cpp             # [Người 3] Thuật toán BaseCase
│   ├── baseline/
│   │   └── bmssp_seq.cpp             # [Người 1] Bản BMSSP tuần tự đối chuẩn 1 luồng
│   └── main.cpp                      # [Người 1 tạo khung, Người 3 & Người 4 hoàn thiện CLI]
│
├── tests/                            # Unit tests, Regression & Sanitizer tests
│   ├── test_atomic_state.cpp         # [Người 4] Kiểm tra lock-free 128-bit CAS & memory order
│   ├── test_d_stress.cpp             # [Người 4] Bắn 32 luồng stress-test ConcurrentFrontierD
│   ├── test_gadget_transform.cpp     # [Người 4] Kiểm tra biến đổi đồ thị bậc <= 2 và hoàn nguyên
│   └── verify_correctness.cpp        # [Người 4] Đối chiếu kết quả với Dijkstra tuần tự (Ground Truth)
│
└── benchmarks/                       # Performance scaling & Profiling scripts
    ├── bench_synthetic.cpp           # [Người 4] Benchmark đồ thị ngẫu nhiên và Scale-Free
    ├── run_scaling.py                # [Người 4] Kịch bản tự động đo Speedup (1, 2, 4, 8, 16, 32 luồng)
    └── profile_vtune.sh              # [Người 4] Kịch bản đo Cache Miss, False Sharing, Lock Contention
```

### 2.2. Ma Trận Phân Quyền (RACI Matrix)
*   **R (Responsible):** Người trực tiếp viết mã nguồn và chịu trách nhiệm chính.
*   **A (Accountable):** Người phê duyệt và chịu trách nhiệm cuối cùng khi merge vào nhánh chính.
*   **C (Consulted):** Người được tham vấn ý kiến kỹ thuật khi thiết kế.
*   **I (Informed):** Người nhận thông báo cập nhật để đồng bộ công việc.

| Module / Tệp Tin | Người 1 (Hạ tầng) | Người 2 (Cấu trúc $\mathcal{D}$) | Người 3 (Thuật toán) | Người 4 (QA & Tối ưu) |
| :--- | :---: | :---: | :---: | :---: |
| `CMakeLists.txt` & Build Toolchain | **R / A** | I | I | C (thêm targets) |
| `include/core/types.hpp` & Atomics | **R / A** | C | C | I (viết test) |
| `include/core/graph.hpp` & Gadget Transform | **R / A** | I | C (sử dụng) | I |
| `include/core/frontier_interface.hpp` | **R / A** | C (triển khai) | C (sử dụng) | I |
| `include/core/memory_pool.hpp` | **R / A** | C (sử dụng) | I | I |
| `src/data_structure/*` ($\mathcal{D}_0, \mathcal{D}_1$) | I | **R / A** | I | C (stress test) |
| `src/algorithm/*` (`FindPivots`, `BMSSP`) | I | I | **R / A** | C (verify test) |
| `tests/*` (TSan, Sanitizers, Invariants) | I | C | C | **R / A** |
| `benchmarks/*` (Speedup, Profiling, VTune)| I | C | C | **R / A** |

---

## 3. Phân Công Nhiệm Vụ Chi Tiết Cho Từng Thành Viên

### 3.1. NGƯỜI 1: Kiến Trúc Sư Hạ Tầng, Biến Đổi Đồ Thị & Parallel Primitives *(Giai đoạn 1 - Làm đầu tiên)*

* **Vai trò:** Xây dựng nền móng bộ nhớ cấp thấp, biến đổi đồ thị bậc hằng số, các thao tác nguyên tử 128-bit và thư viện hàm song song dùng chung cho toàn dự án.
* **Danh sách công việc cụ thể:**
  1. **Khởi tạo hệ thống Build (`CMakeLists.txt`):**
     * Thiết lập chuẩn C++20, cờ tối ưu hóa `-O3 -march=native -DNDEBUG`.
     * Bật cờ bắt buộc `-mcx16` để kích hoạt lệnh vi kiến trúc `lock cmpxchg16b` (128-bit CAS trên x86-64).
     * Cấu hình OpenMP (`find_package(OpenMP REQUIRED)`) và cờ kiểm tra Sanitizer (`-fsanitize=thread -g`).
  2. **Định nghĩa trạng thái đỉnh nguyên tử 128-bit (`include/core/types.hpp`):**
     * Tạo `struct alignas(16) VertexState` gồm `double dist` (8 bytes), `uint32_t pred` (4 bytes), `uint32_t epoch` (4 bytes).
     * Bắt buộc có `static_assert(std::atomic<VertexState>::is_always_lock_free)` để xác thực phần cứng hỗ trợ atomic 128-bit không dùng mutex nội bộ.
     * Hiện thực hóa hàm `relax_edge_atomic` dùng `compare_exchange_weak` với `memory_order_release` / `memory_order_relaxed` để triệt tiêu lỗi *Torn Read/Write*.
  3. **Biểu diễn đồ thị CSR & Module Biến Đổi Gadget (`include/core/graph.hpp`, `src/core/graph.cpp`):**
     * Xây dựng cấu trúc `CSRGraph` (Compressed Sparse Row) lưu trữ mảng `row_ptr`, `col_idx`, `weight`. Áp dụng bố cục Structure of Arrays (SoA) để tránh phân mảnh bộ nhớ.
     * **Triển khai Gadget Constant-Degree Transformation (Bảo đảm lý thuyết Mục 2 bài báo):**
       * Hàm `CSRGraph transform_to_constant_degree(const CSRGraph& original)`: Với mỗi đỉnh $v$ có bậc $d > 2$, thay thế $v$ bằng một chu trình gồm $d$ đỉnh phụ nối với nhau bằng các cạnh có trọng số 0, đưa bậc tối đa của đồ thị về $\le 2$.
       * Hàm `void restore_distances(const std::vector<double>& transformed_dist, std::vector<double>& orig_dist)`: Ánh xạ ngược kết quả khoảng cách từ đồ thị biến đổi về đồ thị gốc.
  4. **Bộ cấp phát bộ nhớ Thread-Local Arena Allocator (`include/core/memory_pool.hpp`):**
     * Xây dựng Memory Pool cấp phát mảng khối bộ nhớ tĩnh cho các Node của Block trong $\mathcal{D}_1$, giúp loại bỏ 100% việc gọi `malloc`/`free` trên Heap toàn cục giữa các luồng.
  5. **Khóa cứng Giao diện Cấu trúc dữ liệu $\mathcal{D}$ (`include/core/frontier_interface.hpp`):**
     * Định nghĩa interface thuần ảo `IFrontierD` chuẩn hóa:
       ```cpp
       class IFrontierD {
       public:
           virtual ~IFrontierD() = default;
           virtual void init(size_t M, double B) = 0;
           virtual void insert(uint32_t v, double dist) = 0;
           virtual void batch_prepend(std::span<const VertexDist> K) = 0;
           virtual bool pull(std::vector<uint32_t>& S_i, double& B_i) = 0;
           virtual bool empty() const = 0;
           virtual void clear() = 0;
       };
       ```
     * Viết class `MutexFrontierD` kế thừa `IFrontierD` (dùng `std::mutex` bảo vệ `std::multimap`) để Người 3 dùng ngay làm mockup.
  6. **Bộ thư viện Parallel Primitives (`include/core/primitives.hpp`):**
     * Khử trùng lặp Frontier: `std::atomic<uint32_t> visited_epoch[n]` dùng `compare_exchange_strong(expected, j, std::memory_order_acq_rel)`.
     * Thread-Local Bucketing & Parallel Prefix Scan gom frontier $W_j$ và tập $K$ với độ phức tạp $\mathcal{O}(N/P + \log P)$.
     * Parallel Sample Sort / Quickselect sắp xếp tập $K$ tăng dần trên nhiều luồng.
  7. **Bản chuẩn tuần tự (`src/baseline/bmssp_seq.cpp`):**
     * Hiện thực hóa BMSSP 1 luồng bám sát Algorithm 1, 2, 3 làm cơ sở đối chuẩn (Baseline) đo tốc độ.

---

### 3.2. NGƯỜI 2: Kỹ Sư Cấu Trúc Dữ Liệu Đồng Thời $\mathcal{D}$ *(Giai đoạn 2 - Làm song song với Người 3)*

* **Vai trò:** Phụ trách hiện thực hóa cấu trúc dữ liệu phân tầng $\mathcal{D}$ ($\mathcal{D}_0$ và $\mathcal{D}_1$) theo Lemma 3.3, đạt thông lượng cao, không deadlock, chống bão chia tách block và duy trì tính đơn điệu khoảng cách.
* **Danh sách công việc cụ thể:**
  1. **Xây dựng tầng $\mathcal{D}_0$ (`src/data_structure/d0_deque.cpp`):**
     * **Lưu ý nguyên tắc:** Bãi bỏ hoàn toàn Treiber Stack (do LIFO đảo ngược thứ tự khoảng cách).
     * Sử dụng `std::deque<Block>` lưu trữ các block tuần tự (mỗi block $\le M$ phần tử).
     * Hàm `batch_prepend(K)`: Nhận tập $K$ đã được sắp xếp tăng dần từ Parallel Sample Sort của Người 1, đóng gói thành các block và đưa vào đầu deque trên luồng Master duy nhất trong $\mathcal{O}(|K|/P + M \log M)$.
  2. **Xây dựng tầng $\mathcal{D}_1$ (`src/data_structure/d1_skiplist.cpp`):**
     * Hiện thực hóa **Concurrent Skip-List** (hoặc B-Link Tree) quản lý các giá trị chặn trên (upper bound) của từng block.
     * Trang bị **Block-Level Spinlock** (`std::atomic_flag` tối ưu `_mm_pause()`) riêng cho từng block để nhiều luồng relax gọi `insert(v, dist)` đồng thời vào các block khác nhau mà không xung đột.
     * Tận dụng `MemoryPool` của Người 1 để cấp phát các nút trong danh sách liên kết của block.
  3. **Triển khai Lazy Deletion (Triệt tiêu 100% Deadlock):**
     * Khi nhận `insert(v, new_dist)`, luồng **tuyệt đối không** tìm block cũ để xóa (loại bỏ hoàn toàn việc giữ 2 khóa). Luồng chỉ tìm block tương ứng với `new_dist` trên Skip-List, chiếm khóa duy nhất của block đó và thêm phần tử vào.
  4. **Cơ chế Chia Tách Block (`SPLIT`) Không Nghẽn & Dọn Rác Trước Khi Tách (Eager Cleanup on Split):**
     * Khi số phần tử trong một block vượt ngưỡng $M$, luồng chuyển cờ trạng thái block sang `SPLITTING` bằng `atomic_flag`.
     * **Quy trình Eager Cleanup chống Split Storm:**
       1. Quét nhanh mảng phần tử trong block, loại bỏ ngay các phần tử đã lỗi thời ($d_{\text{entry}} > \text{state}[v].\text{load().dist}$).
       2. Nếu sau khi dọn rác, số phần tử còn lại $\le M$: Hủy bỏ lệnh split, mở khóa block về `NORMAL`.
       3. Nếu số phần tử vẫn $> M$: Dùng `std::nth_element` lấy phần tử trung vị, tách đôi block thành 2 block con $\approx M/2$, cập nhật upper bound mới vào Skip-List rồi mở khóa.
  5. **Hiện thực hóa hàm trích xuất `pull(S_i, B_i)`:**
     * So sánh khóa chặn trên giữa block đầu $\mathcal{D}_0$ và block đầu $\mathcal{D}_1$ để lấy ra block có khoảng cách nhỏ nhất.
     * **Lazy Cleanup:** Bỏ qua các phần tử có $d_{\text{pulled}} > \text{state}[v].\text{load(std::memory\_order\_acquire).dist}$.
     * Đóng gói tối đa $M$ phần tử hợp lệ vào $S_i$ và cập nhật mốc $B_i$. Trả về `false` nếu cấu trúc rỗng.

---

### 3.3. NGƯỜI 3: Kỹ Sư Lõi Thuật Toán `FINDPIVOTS` & `BMSSP` *(Giai đoạn 2 - Làm song song với Người 2)*

* **Vai trò:** Phụ trách hiện thực hóa các vòng lặp relax song song, phân tích rừng cây tìm Pivots, và vòng lặp đồng bộ cắt lát khoảng cách (Distance-Sliced Synchronization Loop).
* **Danh sách công việc cụ thể:**
  1. **Bộ điều phối cắt ngưỡng độ hạt (`Granularity Cutoff`):**
     * Viết macro / hàm nội tuyến kiểm tra số cạnh hoạt động $|E_{\text{active}}|$:
       * Nếu $|E_{\text{active}}| \ge \Theta_{\text{cutoff}}$ (mặc định 2048 cạnh): Mở vùng đa luồng OpenMP (`#pragma omp parallel`).
       * Nếu $|E_{\text{active}}| < \Theta_{\text{cutoff}}$: Thực thi tuần tự 1 luồng để triệt tiêu chi phí rào chắn đồng bộ ($\approx 1-5\,\mu\text{s}$) tại các tầng đệ quy đáy ($l = 0, 1$).
  2. **Hiện thực hóa hàm `FINDPIVOTS(B, S, k)` (`src/algorithm/find_pivots.cpp`):**
     * **Vòng lặp relax $k$ bước:** Duyệt tuần tự $j = 1 \dots k$. Tại mỗi bước $j$, phân chia tập cạnh đi ra từ frontier $W_{j-1}$ cho các luồng bằng `#pragma omp parallel for schedule(guided)`. Gọi `relax_edge_atomic` và kiểm tra `visited_epoch` để gom frontier mới $W_j$ qua `Parallel Prefix Scan`.
     * **Phân tích rừng $F$ tìm Pivots:** Dựa vào `pred` trong `VertexState`, chia đều các gốc $s \in S$ cho các luồng duyệt độc lập (BFS/DFS) đếm kích thước cây $|T_s|$ (hoàn toàn không tranh chấp). Gốc nào có $|T_s| \ge k$ thì thêm vào tập Pivot $P$.
  3. **Hiện thực hóa hàm đệ quy `BMSSP(l, B, S)` (`src/algorithm/bmssp.cpp`):**
     * Tầng đáy $l = 0$: Gọi `BASECASE` (Dijkstra tuần tự mini-queue).
     * Tầng đệ quy $l \ge 1$: Vòng lặp ngoài `while |U| < k && !D->empty()` chạy tuần tự theo tiến trình khoảng cách tăng dần:
       * Lấy $(S_i, B_i) \leftarrow D\text{->pull()}$.
       * Gọi đệ quy con `BMSSP(l - 1, B_i, S_i)` thu về tập đỉnh hoàn thành $U_i$.
       * **Relax cạnh từ $U_i$ song song:** Dùng `#pragma omp parallel for` quét tập cạnh đi ra từ $U_i$. Phân loại:
         * Đỉnh $v \in S_{\text{active}}$: Đẩy vào buffer cục bộ luồng gom thành tập $K$.
         * Đỉnh $v \notin S$: Gọi trực tiếp `D->insert(v, d')`.
       * Rào chắn đồng bộ: Luồng Master duy nhất gọi `D->batch_prepend(K)` nếu $K \ne \emptyset$.
  4. **Tích hợp Gadget Transformation:** Đảm bảo toàn bộ quy trình nhận đồ thị gốc $\rightarrow$ chuyển đổi qua Gadget $\rightarrow$ chạy `BMSSP` $\rightarrow$ hoàn nguyên khoảng cách về đồ thị gốc hoạt động liền mạch và trong suốt.

---

### 3.4. NGƯỜI 4: Kỹ Sư Kiểm Thử, ThreadSanitizer & Tinh Chỉnh Hiệu Năng *(Giai đoạn 3 - Làm cuối)*

* **Vai trò:** Bảo đảm chất lượng phần mềm đa luồng, kiểm soát race conditions, chứng minh tính đúng đắn toán học tuyệt đối và tối ưu hóa hệ thống trên phần cứng đa lõi.
* **Danh sách công việc cụ thể:**
  1. **Kiểm tra an toàn tương tranh bằng ThreadSanitizer (`tests/run_tsan.sh`):**
     * Thiết lập kịch bản biên dịch với `-fsanitize=thread -g -O1`.
     * Cho chạy toàn bộ test suite trên đồ thị dày để bắt và loại bỏ mọi lỗi *Data Race*, *Torn Read/Write* hoặc *Deadlock*.
  2. **Bộ Stress-Test độc lập cho Cấu trúc $\mathcal{D}$ (`tests/test_d_stress.cpp`):**
     * Viết chương trình mô phỏng bắn từ 16 đến 32 luồng gọi đồng thời hàng triệu thao tác `insert` ngẫu nhiên xen kẽ `batch_prepend` và `pull`.
     * Kiểm tra độ ổn định của cơ chế `SPLITTING`, tính đúng đắn của Eager Cleanup và xác thực tính đơn điệu của cận trên $B_i$.
  3. **Xác thực kết quả đối chứng Ground Truth (`tests/verify_correctness.cpp`):**
     * Viết thuật toán Dijkstra tuần tự độc lập làm trọng tài so sánh.
     * Đối chiếu toàn bộ khoảng cách $\hat{d}[v]$ trên 100% đỉnh tới được:
       $$\max_{v} |d_{\text{parallel}}[v] - d_{\text{dijkstra}}[v]| < 10^{-9}$$
     * Kiểm tra trên các đồ thị đặc biệt: cạnh trọng số 0, chu trình, đồ thị ngắt kết nối và các ca near-tie ($10^{-11}$).
  4. **Thiết lập môi trường Benchmark & Phân tích CPU Profiling (`benchmarks/`):**
     * Cấu hình ràng buộc CPU Core Affinity: thiết lập `OMP_PROC_BIND=close`, `OMP_PLACES=cores` trong kịch bản chạy benchmark để loại bỏ chi phí trễ NUMA.
     * Sử dụng `perf stat` / `perf record` (hoặc Intel VTune) để đo tỷ lệ L1/L3 Cache Miss, số chu kỳ CPU nhàn rỗi do tranh chấp Spinlock.
     * Đo đạc và vẽ biểu đồ **Speedup** và **Parallel Efficiency** theo số luồng ($1, 2, 4, 8, 16, 32$ luồng) trên đồ thị $n = 10^6 \dots 10^7$.
     * Tinh chỉnh thực nghiệm để tìm ra giá trị tối ưu của ngưỡng cắt $\Theta_{\text{cutoff}}$ ($1024, 2048, 4096$) và kích thước block $M$.

---

### 3.5. Tiêu Chí Nghiệm Thu Định Lượng (Definition of Done - DoD)

Trước khi bất kỳ thành viên nào được phép merge mã nguồn vào nhánh `main`, tệp Pull Request bắt buộc phải vượt qua các tiêu chí nghiệm thu định lượng sau:

| Thành viên | Trách nhiệm chính | Tiêu chí nghiệm thu bắt buộc (DoD) |
| :--- | :--- | :--- |
| **Người 1** | Hạ tầng, CSR, Gadget, Atomics | 1. `static_assert(std::atomic<VertexState>::is_always_lock_free)` pass 100% trên hệ thống biên dịch.<br>2. Bản tuần tự `bmssp_seq` cho kết quả khớp bit-exact với Dijkstra trên toàn bộ đồ thị mẫu.<br>3. `MutexFrontierD` hoàn chỉnh, biên dịch độc lập không lỗi.<br>4. Module Gadget Transform bảo toàn 100% đường đi ngắn nhất sau khi hoàn nguyên. |
| **Người 2** | Cấu trúc dữ liệu đồng thời $\mathcal{D}$ | 1. Vượt qua `test_d_stress` với 32 luồng, 10 triệu operations không xuất hiện deadlock hay crash.<br>2. Dãy cận khoảng cách trả về từ `pull` thỏa mãn tính đơn điệu tăng ngặt: $B_1 \le B_2 \le \dots \le B$.<br>3. Cơ chế Eager Cleanup giúp giảm $\ge 50\%$ số lần gọi lệnh `SPLIT` trên đồ thị dày.<br>4. Không rò rỉ bộ nhớ (AddressSanitizer / Valgrind Clean). |
| **Người 3** | Lõi `FindPivots` & `BMSSP` | 1. Biên dịch và chạy hoàn tất vòng lặp BMSSP với mockup `MutexFrontierD`.<br>2. Sai số khoảng cách so với Dijkstra trên đồ thị kiểm thử nhỏ hơn $10^{-9}$ trên toàn bộ đỉnh.<br>3. Cơ chế Granularity Cutoff tự động kích hoạt tuần tự khi $|E_{\text{active}}| < 2048$. |
| **Người 4** | Kiểm thử, QA & Tối ưu | 1. 0 cảnh báo Data Race / Race Condition dưới cờ ThreadSanitizer (`-fsanitize=thread`).<br>2. Đạt độ tăng tốc thực tế (Speedup) $\ge 3.0\times$ trên CPU 8-16 lõi thực với đồ thị $n \ge 10^6$.<br>3. Xuất đầy đủ biểu đồ Speedup, Scaling Curve và báo cáo phân tích hiệu năng L1/L3 Cache. |

---

## 4. Quy Trình Git Workflow & Tích Hợp Cuốn Chiếu

Quy trình phối hợp nhánh Git đảm bảo các thành viên có thể đẩy tiến độ liên tục mà không gây ách tắc:

```
[main] ────────────────────────────────────────────────────────────► (Release v1.0 Parallel BMSSP)
   │                                               ▲            ▲
   ├─► [feat/core-infra] (Người 1) ────────────────┤            │
   │        (Include headers, types, CSR, Mockup)  │            │
   │                                               │            │
   ├─► [feat/bmssp-algo] (Người 3) ────────────────┤ (Merge 1)  │
   │        (Thuật toán chạy với MutexFrontierD)   │            │
   │                                                            │
   └─► [feat/concurrent-d] (Người 2) ───────────────────────────┘ (Merge 2 - Bật cờ USE_CONCURRENT_D)
            (Skip-List D1, D0 Deque, Eager Cleanup)
```

1. **Bước 1 (Nền móng hạ tầng):**
   * Người 1 làm việc trên nhánh `feat/core-infra`.
   * Hoàn thành `include/core/*`, `src/baseline/bmssp_seq.cpp` và khung `CMakeLists.txt`. Tạo PR merge vào `main` sau khi DoD Người 1 được nghiệm thu.
2. **Bước 2 (Phát triển song song độc lập):**
   * Từ `main`, **Người 2** tách nhánh `feat/concurrent-d` (chỉ làm việc trong `include/data_structure/` và `src/data_structure/`).
   * Từ `main`, **Người 3** tách nhánh `feat/bmssp-algo` (chỉ làm việc trong `include/algorithm/` và `src/algorithm/`).
   * Trong `src/main.cpp`, sử dụng cờ tiền xử lý linh hoạt:
     ```cpp
     std::unique_ptr<IFrontierD> frontier;
     #ifdef USE_CONCURRENT_D
         frontier = std::make_unique<ConcurrentFrontierD>(); // Bản thông lượng cao của Người 2
     #else
         frontier = std::make_unique<MutexFrontierD>();      // Bản mockup an toàn của Người 1
     #endif
     ```
3. **Bước 3 (Tích hợp cuốn chiếu):**
   * **Tích hợp đợt 1:** Người 3 hoàn thành logic thuật toán, merge nhánh `feat/bmssp-algo` vào `main`. Hệ thống lúc này đã có bản chạy đa luồng hoàn chỉnh sử dụng `MutexFrontierD`. Người 4 lập tức bắt đầu chạy `verify_correctness` và cấu hình bộ test TSan cho thuật toán.
   * **Tích hợp đợt 2:** Người 2 hoàn thành `ConcurrentFrontierD`, vượt qua bài test độc lập `test_d_stress` của Người 4, sau đó merge `feat/concurrent-d` vào `main`. Người 4 bật cờ `-DUSE_CONCURRENT_D` trong `CMakeLists.txt` để thay thế bản mockup.
   * **Tối ưu hóa tổng thể:** Cả nhóm phối hợp cùng Người 4 chạy profiling, phân tích biểu đồ Speedup và chốt cấu hình tham số hệ thống tối ưu.
