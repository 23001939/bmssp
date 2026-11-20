# Thuật Toán BMSSP Song Song (Parallel Bounded Multi-Source Shortest Path)

Triển khai thuật toán tìm đường đi ngắn nhất nguồn đơn (**Single-Source Shortest Path - SSSP**) song song trên kiến trúc CPU đa lõi (C++20 & OpenMP), đối chiếu trực tiếp với bài báo khoa học STOC 2025 của Duan et al.:
> **"Breaking the Sorting Barrier for Directed Single-Source Shortest Paths"** *(Ran Duan, Jiayi Mao, Xiao Mao, Xinkai Shu, Longhui Yin - arXiv:2504.17033v2)*.

---

## 1. Bản Chất Mô Hình Tính Toán Song Song

### 1.1. Bác bỏ quan niệm sai lầm: "Divide-and-Conquer Task Parallelism"
Trong các tài liệu sơ khai, BMSSP thường bị nhầm lẫn là có thể song song hóa theo mô hình chia để trị dạng nhánh (Task Parallelism tương tự Mergesort). Tuy nhiên:
1. Vòng lặp `while |U| < quota and D != empty` chạy qua các iteration tuần tự $i = 1, 2, \dots$.
2. Tại mỗi iteration, cặp $(S_i, B_i) \leftarrow \text{PULL}(D)$ phụ thuộc trực tiếp vào trạng thái của cấu trúc dữ liệu $D$ sau khi đã relax toàn bộ các cạnh xuất phát từ $U_{i-1}$ của iteration trước.
3. Dãy chặn khoảng cách là đơn điệu tăng ngặt: $B_1 \le B_2 \le \dots \le B$.
4. Lời gọi đệ quy `BMSSP(l-1, Bi, Si)` chỉ có thể được kích hoạt khi $S_i$ và $B_i$ đã được xác định hoàn toàn.

> [!IMPORTANT]
> **Nguyên lý cốt lõi:** Giữa các bước đệ quy tồn tại **sự phụ thuộc dữ liệu tuần tự chặt chẽ (Strict Sequential Distance-Sliced Dependency)**. Do đó, mô hình song song chuẩn xác là **Vòng lặp đồng bộ cắt lát theo khoảng cách (Distance-Sliced Synchronization Loop)** kết hợp với **Song song dữ liệu đồ thị hạt mịn (Fine-Grained Graph Data Parallelism)** bên trong từng vòng lặp.

---

### 1.2. Kiến trúc tổng thể hệ thống

```text
┌────────────────────────────────────────────────────────────────────────┐
│                        TRỤC ĐIỀU PHỐI TUẦN TỰ                          │
│         Quản lý các tầng đệ quy l và tiến trình tăng dần của B_i       │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                   CÁC NGUYÊN THỦY SONG SONG HẠT MỊN                    │
│                                                                        │
│ 1. FINDPIVOTS:   Duyệt cạnh song song OpenMP qua k bước Bellman-Ford   │
│ 2. DEDUPLICATION: Mảng cờ nguyên tử visited_epoch chống trùng frontier  │
│ 3. RELAXATION:   Ghi nguyên tử 128-bit CAS (cmpxchg16b) không khóa     │
│ 4. BUCKETING:    Thread-local buffer gom tập K qua Parallel Prefix Scan │
│ 5. SORTING:      Parallel Sample Sort / Quickselect trên tập K         │
│ 6. FRONTIER D:   Hàng đợi phân tầng D_0 / D_1 với cơ chế Lazy Deletion │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Cấu Trúc Thư Mục & Vai Trò Các Tệp

```text
bmssp_parallel/
├── CMakeLists.txt                         # Cấu hình biên dịch dự án
├── build_and_test.py                      # Kịch bản 1-click tự động build và chạy test
├── README.md                              # Tài liệu kỹ thuật chi tiết
│
├── include/
│   ├── core/
│   │   ├── types.hpp                      # Trạng thái đỉnh nguyên tử 128-bit (VertexState)
│   │   ├── graph.hpp                      # Đồ thị CSR và Gadget Constant-Degree Transform
│   │   ├── memory_pool.hpp                # Thread-local Memory Arena cho các khối dữ liệu
│   │   ├── frontier_interface.hpp         # Giao diện trừu tượng IFrontierD & MutexFrontierD
│   │   └── primitives.hpp                 # Parallel Scan, Parallel Sort, Epoch Deduplication
│   ├── data_structure/
│   │   └── concurrent_frontier_d.hpp      # Cấu trúc D phân tầng (Lemma 3.3) đa luồng
│   ├── algorithm/
│   │   ├── find_pivots.hpp                # Khai báo Algorithm 1: FindPivots song song
│   │   ├── base_case.hpp                  # Khai báo Algorithm 2: BaseCase cục bộ
│   │   └── bmssp.hpp                      # Khai báo Algorithm 3: BMSSP đệ quy đa tầng
│   └── baseline/
│       └── bmssp_seq.hpp                  # Bản đối chuẩn tuần tự 1 luồng
│
├── src/
│   ├── core/
│   │   └── graph.cpp                      # Đọc file, nạp CSR, Gadget Transform
│   ├── algorithm/
│   │   ├── find_pivots.cpp                # Hiện thực FindPivots song song
│   │   ├── base_case.cpp                  # Hiện thực BaseCase Dijkstra cục bộ
│   │   └── bmssp.cpp                      # Hiện thực vòng lặp BMSSP cắt lát khoảng cách
│   └── baseline/
│       └── bmssp_seq.cpp                  # Hiện thực Dijkstra & BMSSP tuần tự
│
└── tests/
    ├── test_person1_comprehensive.cpp     # Kiểm thử hạ tầng nguyên tử và bộ nhớ
    └── verify_parallel_bmssp.cpp          # Đối chiếu độ chính xác với Dijkstra Oracle
```

---

## 3. Mã Giả & Giải Thích Chi Tiết Từng Thành Phần

### 3.1. Trạng Thái Đỉnh Nguyên Tử 128-bit (`types.hpp`)
Để triệt tiêu lỗi *Torn Read/Write* giữa khoảng cách (64-bit) và đỉnh cha (32-bit), trạng thái đỉnh được đóng gói trong cấu trúc 16 bytes căn chỉnh 128-bit:

```text
Structure VertexState (16 bytes, alignas(16)):
    double   dist;    // 8 bytes: khoảng cách ngắn nhất hiện tại
    uint32_t pred;    // 4 bytes: đỉnh cha trên cây đường đi ngắn nhất
    uint32_t epoch;   // 4 bytes: thế hệ relaxation chống ABA
```

#### Mã giả thao tác Relax nguyên tử:
```text
Algorithm relax_edge_atomic(target, new_dist, from_u, current_epoch):
    curr = target.load(relaxed)
    while new_dist < curr.dist:
        next = VertexState(dist: new_dist, pred: from_u, epoch: current_epoch)
        if atomic_compare_exchange(target, expected: curr, desired: next):
            return true   // Thư giãn thành công, cập nhật nguyên tử cả dist và pred
    return false          // Không có cải thiện khoảng cách
```

---

### 3.2. Cấu Trúc Hàng Đợi Ưu Tiên Theo Lô $\mathcal{D}$ (`frontier_interface.hpp` & `concurrent_frontier_d.hpp`)
Hiện thực cấu trúc dữ liệu theo **Lemma 3.3**:
*   $\mathcal{D}_0$ (Sequential Block Deque): Lưu các khối từ `batch_prepend(K)`. Chỉ do luồng Master gọi sau rào chắn $\implies$ không tranh chấp khóa.
*   $\mathcal{D}_1$ (Partitioned Concurrent Blocks): Lưu các đỉnh do các luồng relax gọi `insert(v, dist)` đồng thời. Mỗi khối có khóa hạt mịn riêng.
*   **Lazy Deletion:** Khi relax khoảng cách tốt hơn, luồng chỉ ghi vào khối mới, không tìm xóa khối cũ. Khi `pull`, phần tử lỗi thời tự động bị bỏ qua.

#### Mã giả thao tác `pull(S_i, B_i)`:
```text
Algorithm pull(S_i, B_i):
    Khóa global_mtx
    candidates = []
    
    1. Thu thập tối đa 2M phần tử từ đầu D_0
    2. Thu thập tối đa 2M phần tử từ đầu D_1
    
    Nếu candidates rỗng:
        B_i = B_global
        Trả về false (Hàng đợi rỗng)
        
    Sắp xếp candidates theo khoảng cách tăng dần
    count = min(|candidates|, M)
    S_i = { v | (v, d) trong count phần tử đầu tiên }
    
    Nếu count < |candidates|:
        B_i = candidates[count].dist  // Cận là khoảng cách phần tử nhỏ thứ M+1
        Đưa các phần tử còn lại vào đầu D_0
    Ngược lại:
        B_i = khoảng cách nhỏ nhất tiếp theo trong D_0 và D_1 (hoặc B_global nếu hết)
        
    Trả về true
```

---

### 3.3. Algorithm 1: `find_pivots_parallel` (`find_pivots.cpp`)
Chạy $k$ bước Bellman-Ford song song từ tập nguồn $S$ để rút gọn thành tập đỉnh then chốt $P$ ($|P| \le |vis| / k$).

```text
Algorithm find_pivots_parallel(graph, B, S, k, states, deduplicator):
    vis = S
    active = S
    Với mỗi s trong S: root[s] = s

    Lặp step từ 1 đến k:
        Nếu active rỗng: break
        
        // Song song hóa trên tập cạnh đi ra từ active
        #pragma omp parallel for schedule(guided)
        Với mỗi u trong active:
            root_u = root[u]
            Với mỗi cạnh (u, v, w) của u:
                new_dist = states[u].dist + w
                Nếu new_dist < B:
                    curr_v = states[v].dist
                    Nếu new_dist <= curr_v:
                        Nếu new_dist < curr_v:
                            relax_edge_atomic(states[v], new_dist, u, step)
                        root[v] = root_u
                        Nếu deduplicator.try_claim(v, step):
                            thread_local_buffer.push(v)

        Gom thread_local_buffer thành next_active
        vis.insert(next_active)
        
        // Cắt ngưỡng sớm (Early Cutoff)
        Nếu |vis| > k * |S|:
            Trả về (Pivots: S, Visited: vis)
            
        active = next_active

    // Đếm kích thước cây con arborescence của từng nguồn
    Với mỗi v trong vis:
        treesize[root[v]]++

    P = { s trong S | treesize[s] >= k }
    Trả về (Pivots: P, Visited: vis)
```

---

### 3.4. Algorithm 2: `base_case` (`base_case.cpp`)
Xử lý tầng cơ sở $l = 0$ bằng Dijkstra cục bộ giới hạn lấy tối đa $k$ đỉnh có khoảng cách $< B$.

```text
Algorithm base_case(graph, B, source, k, states):
    Khởi tạo Min-Heap heap; complete = []
    
    // Đỉnh nguồn luôn được nạp làm mốc xuất phát của khoảng [d_src, B)
    heap.emplace(states[source].dist, source)

    Trong khi heap không rỗng và |complete| < k + 1:
        (d, u) = heap.pop()
        Nếu d > states[u].dist: continue

        complete.push(u)

        Với mỗi cạnh (u, v, w) của u:
            new_dist = d + w
            curr_v = states[v].dist
            Nếu new_dist <= curr_v và new_dist < B:
                Nếu new_dist < curr_v:
                    relax_edge_atomic(states[v], new_dist, u, 0)
                heap.emplace(new_dist, v)

    Nếu |complete| <= k:
        Trả về (new_bound: B, completed: complete)
    Ngược lại:
        // Đỉnh thứ k+1 xác định cận mới co lại
        last_v = complete.pop_back()
        new_bound = states[last_v].dist
        Trả về (new_bound: new_bound, completed: complete)
```

---

### 3.5. Algorithm 3: `bmssp_parallel` (`bmssp.cpp`)
Vòng lặp cắt lát khoảng cách chính điều phối các luồng tính toán song song.

```text
Algorithm bmssp_rec(level, B, S):
    Nếu S rỗng: Trả về (B, [])
    Nếu level == 0: Trả về base_case(B, S[0], k)

    // Bước 1: Tìm Pivots song song
    (P, bellman_vis) = find_pivots_parallel(B, S, k)

    // Bước 2: Khởi tạo hàng đợi D tầng level
    batch_size = 2^((level - 1) * t)
    D = khởi tạo FrontierD(batch_size, B)
    Với mỗi p trong P:
        D.insert(p, states[p].dist)

    quota = k * 2^(level * t)
    complete = []

    // Bước 3: Vòng lặp cắt lát khoảng cách
    Trong khi |complete| < quota và !D.empty():
        (miniS, trying_B) = D.pull()
        
        // Đệ quy tầng dưới
        (complete_B, nw_complete) = bmssp_rec(level - 1, trying_B, miniS)
        complete.insert(nw_complete)

        // Bước 4: Thư giãn cạnh song song từ tập đỉnh mới hoàn thành
        #pragma omp parallel for schedule(guided)
        Với mỗi u trong nw_complete:
            du = states[u].dist
            Với mỗi cạnh (u, v, w) của u:
                new_dist = du + w
                curr_v = states[v].dist
                Nếu new_dist <= curr_v và new_dist < B:
                    Nếu new_dist < curr_v:
                        relax_edge_atomic(states[v], new_dist, u, level)
                    Nếu new_dist >= trying_B:
                        D.insert(v, new_dist)
                    Ngược lại:
                        local_k.push(v, new_dist)

        // Bổ sung các đỉnh miniS chưa xử lý xong
        Với mỗi x trong miniS:
            Nếu x chưa hoàn thành và states[x].dist < B:
                local_k.push(x, states[x].dist)

        Nếu local_k không rỗng:
            parallel_sample_sort(local_k)
            D.batch_prepend(local_k)

        last_complete_B = complete_B

    retB = D.empty() ? B : last_complete_B

    // Bước 5: Bổ sung các đỉnh hoàn thành từ Bellman-Ford
    Với mỗi x trong bellman_vis:
        Nếu x chưa đánh dấu và states[x].dist < retB:
            complete.push(x)

    Trả về (retB, complete)
```

---

## 4. Hướng Dẫn Biên Dịch & Chạy Kiểm Thử

### 4.1. Yêu cầu hệ thống
* Trình biên dịch C++ hỗ trợ **C++20** (GCC 12+, Clang 15+).
* Hỗ trợ chỉ lệnh kiến trúc x86-64: `-mcx16` (cho lệnh `lock cmpxchg16b`).
* Hỗ trợ **OpenMP** (`-fopenmp`).
* Python 3.8+ (để chạy kịch bản tự động hóa).

### 4.2. Chạy kiểm thử tự động 1-Click
Từ thư mục gốc repository:

```bash
python3 bmssp_parallel/build_and_test.py
```

Kịch bản tự động biên dịch toàn bộ các file đối tượng và thực thi 2 bộ kiểm thử:
1. **Person 1 Test Suite:** Kiểm tra 128-bit atomic, CSR Graph, Memory Pool, và Parallel Primitives (153.932 checks).
2. **Parallel BMSSP Verification Suite:** Đối chiếu kết quả với thuật toán Dijkstra tuần tự độc lập (Ground Truth Oracle) trên toàn bộ các dạng đồ thị.

---

## 5. Phân Tích Độ Phức Tạp Work-Span

Theo lý thuyết mô hình tính toán song song PRAM:

* **Tổng lượng công việc ($T_1$ - Work):**
  $$T_1 = \mathcal{O}\left(m \log^{2/3} n\right)$$
  Tương đương phiên bản tuần tự của bài báo STOC 2025, đảm bảo tính hiệu quả công việc (*Work-Efficient*).
* **Chiều sâu đường găng ($T_\infty$ - Span):**
  $$T_\infty = \mathcal{O}\left(2^{\log^{1/3} n} \log^2 n\right)$$
* **Hệ số tăng tốc lý thuyết tối đa (Maximum Speedup Potential):**
  $$\mathcal{P}_{\text{max}} = \frac{T_1}{T_\infty} = \mathcal{O}\left(\frac{m}{2^{\log^{1/3} n} \log^{4/3} n}\right)$$
  Trên các đồ thị quy mô lớn ($n \ge 10^7, m \ge 10^8$), dư địa tăng tốc song song của thuật toán có thể đạt trên **$100.000\times$**.
