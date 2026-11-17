# Tài Liệu Thiết Kế Kỹ Thuật: Song Song Hóa Thuật Toán BMSSP (Bounded Multi-Source Shortest Path)

Tài liệu này cung cấp thiết kế kỹ thuật, phân tích độ phức tạp Work-Span, cơ chế đồng bộ không khóa (lock-free) và khóa hạt mịn (fine-grained locking) cho việc hiện thực hóa thuật toán **BMSSP** trên phần cứng đa lõi, đối chiếu trực tiếp với bài báo gốc *"Breaking the Sorting Barrier for Directed Single-Source Shortest Paths"* (Duan, Ji, Mao - arXiv:2504.17033v2).

---

## 1. Bản Chất Mô Hình Tính Toán Song Song Của BMSSP

### 1.1. Bác Bỏ Quan Niệm Sai Lầm: "Divide-and-Conquer Task Parallelism"
Trong các tài liệu sơ khai, BMSSP thường bị nhầm lẫn là có thể song song hóa theo mô hình chia để trị dạng nhánh (Branching Divide-and-Conquer Task Parallelism tương tự Mergesort). Tuy nhiên, đối chiếu với **Thuật toán 3** (`BMSSP(l, B, S)`):

1. Vòng lặp `while |U| < k and D != empty` chạy qua các iteration tuần tự $i = 1, 2, \dots$.
2. Tại mỗi iteration $i$, cặp $(S_i, B_i) \leftarrow \text{PULL}(D)$ phụ thuộc trực tiếp vào trạng thái của cấu trúc dữ liệu $D$ sau khi đã relax toàn bộ các cạnh xuất phát từ $U_{i-1}$ của iteration trước.
3. Dãy chặn trên khoảng cách là đơn điệu tăng ngặt: $B_1 \le B_2 \le \dots \le B$.
4. Lời gọi đệ quy `BMSSP(l-1, Bi, Si)` chỉ có thể được kích hoạt khi $S_i$ và $B_i$ đã được xác định hoàn toàn.

> [!IMPORTANT]
> **Kết luận học thuật:** Giữa các lần lặp $i$ và giữa các lời gọi đệ quy ở cùng tầng hoặc khác tầng tồn tại **sự phụ thuộc dữ liệu tuần tự chặt chẽ (Strict Sequential Distance-Sliced Dependency)**. Do đó, **hoàn toàn không thể rẽ nhánh thực thi song song các hàm `BMSSP` con độc lập**.

### 1.2. Mô Hình Chính Xác: Distance-Sliced Data Parallelism
Mô hình tính toán song song chuẩn xác cho BMSSP là **Vòng lặp đồng bộ cắt lát theo khoảng cách (Distance-Sliced Synchronization Loop)** kết hợp với **Song song dữ liệu đồ thị hạt mịn (Fine-Grained Graph Data Parallelism)**:
*   **Trục tuần tự (Outer Sequential Loop):** Quản lý tiến trình tăng dần của cận khoảng cách $B_i$ và các tầng đệ quy $l$.
*   **Trục song song (Inner Parallel Primitives):** Song song hóa dữ liệu bên trong từng iteration:
    1.  Song song hóa vòng lặp relax cạnh trong `FINDPIVOTS`.
    2.  Song song hóa phân rã rừng cây và tính kích thước cây con trong `FINDPIVOTS`.
    3.  Song song hóa quá trình duyệt cạnh kề và relax từ tập đỉnh hoàn thành $U_i$ trong `BMSSP`.
    4.  Song song hóa phân loại đỉnh vào các bucket $K$ (Parallel Bucketing) và sắp xếp mẫu (Parallel Sample Sort).

### 1.3. Cơ Chế Cắt Ngưỡng Độ Hạt (Granularity Cutoff Mechanism)
Ở các tầng đệ quy thấp ($l = 0, 1$), kích thước tập nguồn $|S|$ và số đỉnh duyệt $|W|$ rất nhỏ ($k = \mathcal{O}(\log^{1/3} n) \approx 5 - 30$ đỉnh). Nếu kích hoạt luồng song song (OpenMP/TBB task) tại các quy mô này, độ trễ sinh luồng và đồng bộ rào chắn (barrier latency $\approx 1 - 5\,\mu\text{s}$) sẽ vượt xa thời gian tính toán tuần tự ($< 100\,\text{ns}$).

**Quy tắc cắt ngưỡng:**
$$\text{Execution Mode} = \begin{cases} \text{Parallel (OpenMP/TBB)} & \text{khi } |E_{\text{active}}| \ge \Theta_{\text{cutoff}} \\ \text{Sequential (Single Thread)} & \text{khi } |E_{\text{active}}| < \Theta_{\text{cutoff}} \end{cases}$$
Trong đó $\Theta_{\text{cutoff}}$ được thiết lập mặc định là **$2048$ cạnh** (tối ưu hóa theo kích thước cache L1/L2 và chi phí luồng trên CPU x86-64).

---

## 2. Phân Tích Kỹ Thuật Các Vùng Song Song Hóa

### 2.1. Hàm `FINDPIVOTS(B, S, k)` (Thuật toán 1)

#### A. Vòng lặp Relax $k$ Bước (Dòng 4–11 trong bài báo)
*   **Đặc điểm:** Vòng lặp chính chạy $k$ bước tuần tự ($j = 1 \dots k$). Tuy nhiên, tại mỗi bước $j$, việc duyệt toàn bộ các cạnh xuất phát từ tập frontier hiện tại $W_{j-1}$ là hoàn toàn độc lập:
    $$E_{\text{active}}^{(j)} = \{(u, v) \in E \mid u \in W_{j-1}\}$$
*   **Thao tác song song:**
    1.  Phân chia mảng cạnh $E_{\text{active}}^{(j)}$ cho các luồng xử lý đồng thời (`#pragma omp parallel for schedule(guided)`).
    2.  Cập nhật khoảng cách tạm thời $\hat{d}[v] \leftarrow \min(\hat{d}[v], \hat{d}[u] + w_{uv})$.
    3.  Để tránh xung đột ghi và rách dữ liệu (torn write) giữa khoảng cách $\hat{d}[v]$ và đỉnh cha $\text{pred}[v]$, sử dụng thao tác nguyên tử 128-bit CAS (chi tiết tại Mục 3.2).
*   **Khử trùng lặp đỉnh vào Frontier mới ($W_j$ Deduplication):**
    *   Nhiều luồng có thể relax thành công đến cùng một đỉnh $v$ trong cùng một bước $j$. Nếu đẩy trùng lặp $v$ vào $W_j$, kích thước $|W_j|$ sẽ bùng nổ lũy thừa.
    *   **Giải pháp:** Sử dụng mảng dấu thế hệ nguyên tử `std::atomic<uint32_t> visited_epoch[n]`. Một luồng chỉ được phép đẩy $v$ vào buffer cục bộ khi và chỉ khi:
        ```cpp
        uint32_t expected = j - 1;
        if (visited_epoch[v].compare_exchange_strong(expected, j, std::memory_order_acq_rel)) {
            thread_local_buffer.push_back(v);
        }
        ```
    *   Sau khi hoàn tất vòng lặp cạnh, ghép các `thread_local_buffer` thành $W_j$ thông qua phép quét cộng song song (Parallel Prefix Sum / Scan) với độ phức tạp $\mathcal{O}(|W_j| / P + \log P)$.

#### B. Phân Tích Rừng $F$ và Xác Định Pivots (Dòng 14–17 trong bài báo)
*   **Đặc điểm:** Đồ thị $F = \{(p(u), u) \mid u \in W \setminus S\}$ tạo thành một rừng cây có hướng (directed arborescence) với gốc là các đỉnh nguồn $s \in S$.
*   **Thuật toán song song:**
    1.  Mỗi nút $u \in W$ có duy nhất một cạnh cha $p(u)$, do đó không có chu trình.
    2.  Số lượng cây con chính bằng số lượng nguồn $|S|$. Vì $|S| \le k^2$, ta gán mỗi gốc $s \in S$ cho một luồng độc lập.
    3.  Mỗi luồng thực hiện duyệt (BFS/DFS) trên cây con mọc từ gốc $s$ để đếm kích thước $|T_s|$. Vì các cây con rời nhau hoàn toàn về tập đỉnh, thao tác này **hoàn toàn không có tranh chấp (embarrassingly parallel)**.
    4.  Nếu $|T_s| \ge k$, đỉnh $s$ được đưa vào danh sách Pivot $P$ thông qua atomic counter hoặc lock-free collection.

---

### 2.2. Giai Đoạn Relax Cạnh Trong `BMSSP` (Thuật toán 3)

#### A. Duyệt Cạnh Từ Tập Hoàn Thành $U_i$ (Dòng 14–20 trong bài báo)
*   **Đặc điểm:** Sau khi lời gọi đệ quy tầng dưới trả về tập $U_i$, thuật toán duyệt toàn bộ các cạnh xuất phát từ các đỉnh $u \in U_i$:
    $$E_{\text{out}}(U_i) = \{(u, v) \in E \mid u \in U_i\}$$
*   **Phân loại đỉnh đích $v$:**
    1.  Nếu $\hat{d}'[v] = \hat{d}[u] + w_{uv} > B$: Bỏ qua (vượt cận trên).
    2.  Nếu $v \in S_{\text{active}}$ (đỉnh nguồn đang hoạt động): Đưa $v$ vào tập $K$ để chuẩn bị prepend vào $\mathcal{D}_0$.
    3.  Nếu $v \notin S$: Thực hiện $D.\text{Insert}(v, \hat{d}'[v])$.

#### B. Cơ Chế Gom Cụm $K$ Bằng Thread-Local Bucketing
*   Để tránh tranh chấp khi nhiều luồng cùng ghi vào tập $K$, mỗi luồng sở hữu một mảng buffer riêng: `thread_local std::vector<VertexDist> local_K`.
*   Tại rào chắn đồng bộ (Synchronization Barrier), kích thước của từng `local_K` được tính toán, và mảng $K$ toàn cục được cấp phát liền kề. Phép song song song song quét tiền tố (Parallel Prefix Scan) xác định vị trí offset ghi của từng luồng, cho phép copy dữ liệu vào mảng $K$ với thông lượng băng thông tối đa mà không cần khóa.

---

## 3. Các Nút Thắt Cổ Chai (Bottlenecks) và Giải Pháp Kỹ Thuật

| Nút thắt cổ chai (Bottleneck) | Nguyên nhân gốc rễ | Hậu quả kỹ thuật | Giải pháp chuẩn mực (Proposed Solution) |
| :--- | :--- | :--- | :--- |
| **Torn Reads / Writes trên trạng thái đỉnh** | `dist` (64-bit) và `pred` (32-bit) ghi không nguyên tử | Chu trình giả, hỏng cây đường đi ngắn nhất | **Atomic 128-bit CAS** (`cmpxchg16b`) trên struct 16-byte căn chỉnh bộ nhớ. |
| **Deadlock giữa các Block trong $\mathcal{D}$** | Di chuyển đỉnh $v$ từ Block cũ sang Block mới đồng thời giữ 2 khóa | Khóa xoay vòng (Circular Wait), treo toàn bộ luồng | **Lazy Deletion:** Chỉ chèn vào Block mới; ghi nhận mốc khoảng cách tối ưu toàn cục. |
| **False Sharing trên Cache Line** | Các biến trạng thái của các đỉnh liền kề nằm chung Cache Line 64-byte | Cache Invalidation liên tục giữa các Core, sụt giảm IPC | Đệm cấu trúc dữ liệu luồng (Padding) & gom mảng theo cấu trúc SoA (Structure of Arrays). |
| **Barrier Serialization** | Sự phụ thuộc thứ tự khoảng cách $B_i$ giữa các iteration | Luồng bị rảnh rỗi tại barrier nếu tải không đều | **Dynamic Work-Stealing** & Granularity Cutoff để chuyển tuần tự khi tải nhỏ. |

---

### 3.1. Chi Tiết Giải Pháp: Nguyên Tử Hóa Trạng Thái Đỉnh (128-bit Atomic Relaxation)
Để đảm bảo tính nhất quán giữa khoảng cách $\hat{d}[v]$ và đỉnh cha $\text{pred}[v]$, cấu trúc trạng thái đỉnh phải được căn chỉnh 16 bytes:

```cpp
#include <atomic>
#include <cstdint>

struct alignas(16) VertexState {
    double dist;        // 8 bytes
    uint32_t pred;      // 4 bytes
    uint32_t epoch;     // 4 bytes: thế hệ relaxation tránh ABA
};

// Đảm bảo phần cứng hỗ trợ lock-free 128-bit CAS (cmpxchg16b trên x86-64)
static_assert(std::atomic<VertexState>::is_always_lock_free, 
              "128-bit atomic CAS phai duoc ho tro tren phan cung");

inline bool relax_edge_atomic(std::atomic<VertexState>& target, 
                              double new_dist, uint32_t from_u, uint32_t current_epoch) {
    VertexState curr = target.load(std::memory_order_relaxed);
    while (new_dist < curr.dist) {
        VertexState next;
        next.dist = new_dist;
        next.pred = from_u;
        next.epoch = current_epoch;
        
        // Ghi nguyen tu dong thoi ca dist va pred
        if (target.compare_exchange_weak(curr, next, 
                                         std::memory_order_release, 
                                         std::memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}
```

---

## 4. Thiết Kế Chi Tiết Cấu Trúc Dữ Liệu Đồng Thời $\mathcal{D}$ (Lemma 3.3)

Bài báo chứng minh sự tồn tại của cấu trúc dữ liệu $D$ thỏa mãn:
1.  Chứa tối đa $M$ phần tử trên mỗi block.
2.  Hỗ trợ `Insert` trong thời gian $\mathcal{O}(\log M)$.
3.  Hỗ trợ `BatchPrepend` tập $K$ trong thời gian $\mathcal{O}(|K| + M \log M)$.
4.  Hỗ trợ `Pull` trích xuất tập phần tử nhỏ nhất trong thời gian $\mathcal{O}(M)$.

### 4.1. Tách Biệt Kiến Trúc: $\mathcal{D}_0$ và $\mathcal{D}_1$
*   **$\mathcal{D}_0$ (Sequential Block Vector):** Lưu trữ các block sinh ra từ `BatchPrepend`.
*   **$\mathcal{D}_1$ (Concurrent Skip-List + Block Spinlocks):** Lưu trữ các phần tử sinh ra từ thao tác `Insert` thông thường của các luồng relax.

```
                      KIẾN TRÚC TỔNG THỂ CẤU TRÚC DỮ LIỆU D
                      
       [ BATCHPREPEND (Tập K) ]                  [ INSERT (Cạnh relax (u, v)) ]
                  │                                            │
        (Master Thread Duy Nhất)                     (Nhiều Luồng Đồng Thời)
                  │                                            │
                  ▼                                            ▼
       ┌─────────────────────┐                   ┌───────────────────────────────┐
       │   D_0: Block Deque  │                   │  D_1: Concurrent Skip-List    │
       │ (Mỗi block <= M pt) │                   │  (Quản lý Upper Bound Blocks) │
       └──────────┬──────────┘                   └───────────────┬───────────────┘
                  │                                              │
                  │                                              ▼
                  │                              ┌───────────────────────────────┐
                  │                              │   Block Cần Chèn (Spinlock)   │
                  │                              │  - Cặp khóa-giá trị           │
                  │                              │  - Lazy Deletion Flag         │
                  │                              └───────────────┬───────────────┘
                  │                                              │
                  └──────────────────────┬───────────────────────┘
                                         │
                                         ▼
                               [ THAO TÁC PULL(D) ]
             (So sánh Upper Bound giữa D_0.front() và D_1.first_block())
                                         │
                                         ▼
                  ┌──────────────────────────────────────────────┐
                  │ 1. Trích xuất Block có khóa nhỏ hơn          │
                  │ 2. Lazy Cleanup: Bỏ qua đỉnh có d > d_best   │
                  │ 3. Trả về tập S_i và mốc khoảng cách B_i     │
                  └──────────────────────────────────────────────┘
```

---

### 4.2. Khắc Phục Thiết Kế $\mathcal{D}_0$: Bãi Bỏ Treiber Stack
*   **Sai lầm của Treiber Stack:** Treiber Stack hoạt động theo nguyên lý LIFO (Last-In, First-Out). Khi thực hiện prepend các block, thứ tự khoảng cách tăng dần giữa các block sẽ bị đảo ngược, phá hủy hoàn toàn tính bất biến đơn điệu (monotonicity invariant) của hàm `PULL`.
*   **Bản chất đồng bộ của `BatchPrepend` trong Thuật toán 3:**
    Theo Dòng 21–22 của Thuật toán 3:
    $$\text{Nếu } K \ne \emptyset \text{ thì } D.\text{BatchPrepend}(K)$$
    Thao tác này **chỉ được gọi đúng một lần bởi Luồng Master** sau khi toàn bộ các luồng con đã hoàn thành bước duyệt cạnh và vượt qua rào chắn đồng bộ! Do đó, **hoàn toàn không có tranh chấp đa luồng xảy ra đồng thời trên $\mathcal{D}_0$**.
*   **Thiết kế chuẩn:**
    1.  Tập $K$ được phân chia thành các đoạn kích thước $M$.
    2.  Các luồng chạy **Parallel Sample Sort** hoặc **Parallel Quickselect** để sắp xếp $K$ theo khoảng cách tăng dần.
    3.  Luồng Master đóng gói các đoạn thành các Block và đưa vào đầu hàng đợi `std::deque<Block>` của $\mathcal{D}_0$ trong $\mathcal{O}(|K| / P + M \log M)$.

---

### 4.3. Thiết Kế $\mathcal{D}_1$ Với Cơ Chế Lazy Deletion (Tránh Deadlock Tuyệt Đối)
Khi cạnh $(u, v)$ được relax, khoảng cách của $v$ giảm từ $d_{\text{old}}$ xuống $d_{\text{new}}$. Nếu tìm đến block chứa $d_{\text{old}}$ để xóa rồi khóa block chứa $d_{\text{new}}$ để chèn, hai luồng thao tác chéo nhau sẽ dẫn đến **Deadlock vòng tròn**.

**Thiết kế Lazy Deletion:**
1.  **Không bao giờ xóa ngay lập tức:** Khi đỉnh $v$ có khoảng cách tốt hơn, chỉ thực hiện ghi giá trị mới vào mảng nguyên tử `best_known_dist[v]`.
2.  **Chèn độc lập:** Luồng tìm kiếm block tương ứng với $d_{\text{new}}$ trên Concurrent Skip-List, chiếm `Block-Level Spinlock` duy nhất của block đó và thêm phần tử $(v, d_{\text{new}})$ vào danh sách của block.
3.  **Dọn dẹp rác khi `PULL`:** Khi hàm `PULL` lấy ra một khối phần tử từ $\mathcal{D}_0$ hoặc $\mathcal{D}_1$:
    *   Mỗi phần tử $(v, d_{\text{pulled}})$ được kiểm tra:
        $$\text{Nếu } d_{\text{pulled}} > \text{best\_known\_dist}[v] \implies \text{BỎ QUA (Phần tử rác đã lỗi thời)}$$
    *   Thao tác này loại bỏ $100\%$ việc giữ đồng thời 2 khóa, triệt tiêu nguy cơ Deadlock ở cấp độ kiến trúc.

---

### 4.4. Thao Tác Chia Tách Block (`SPLIT`) Không Nghẽn
Khi số phần tử của một block trong $\mathcal{D}_1$ vượt quá giới hạn $M$:
1.  Luồng chèn phần tử gây tràn sẽ cố gắng chuyển cờ trạng thái của block từ `NORMAL` sang `SPLITTING` bằng `atomic_flag`.
2.  Nếu thành công, luồng này chịu trách nhiệm:
    *   Tìm phần tử trung vị (median) bằng `std::nth_element`.
    *   Tách block thành hai block con có kích thước $\approx M/2$.
    *   Cập nhật khóa chặn trên (upper bound) của block mới vào Concurrent Skip-List.
    *   Mở khóa và chuyển trạng thái về `NORMAL`.
3.  Các luồng khác nếu cần chèn vào block trong trạng thái `SPLITTING` sẽ thực hiện `_mm_pause()` (spin nhàn rỗi ngắn) hoặc chuyển sang xử lý cạnh tiếp theo.

---

## 5. Phân Tích Lý Thuyết Work-Span ($T_1, T_\infty$)

Áp dụng lý thuyết mô hình PRAM và Work-Span Analysis:

### 5.1. Tổng Lượng Công Việc ($T_1$ - Work)
Tổng số phép tính tuần tự của thuật toán BMSSP song song tương đương với phiên bản tuần tự của bài báo:
$$T_1 = \mathcal{O}\left(m \log^{2/3} n\right)$$
Các chi phí phụ trội do CAS, rào chắn đồng bộ và lazy deletion được bù trừ bởi việc triệt tiêu chi phí tái cân bằng cây toàn cục, duy trì tính hiệu quả công việc (**Work-Efficient Parallelism**).

### 5.2. Chiều Sâu Đường Găng ($T_\infty$ - Span)
Span của thuật toán bị giới hạn bởi:
1.  Số tầng đệ quy $L = \left\lceil \frac{\log n}{t} \right\rceil = \mathcal{O}(\log^{2/3} n)$ với $t = \log^{1/3} n$.
2.  Tại mỗi tầng đệ quy, số vòng lặp `while` tối đa là $\mathcal{O}(2^t) = \mathcal{O}(2^{\log^{1/3} n})$.
3.  Bên trong mỗi iteration, hàm `FINDPIVOTS` thực hiện $k = \log^{1/3} n$ bước relax tuần tự.
4.  Mỗi bước relax cạnh song song có Span quy đổi bằng phép giảm hoặc quét tiền tố: $\mathcal{O}(\log (\Delta_{\text{max}}))$ với $\Delta_{\text{max}}$ là bậc đỉnh tối đa.

Do đó, tổng Span của thuật toán là:
$$T_\infty = \mathcal{O}\left(L \cdot 2^t \cdot k \cdot \log n\right) = \mathcal{O}\left(\log^{2/3} n \cdot 2^{\log^{1/3} n} \cdot \log^{1/3} n \cdot \log n\right) = \mathcal{O}\left(2^{\log^{1/3} n} \log^2 n\right)$$

### 5.3. Khả Năng Mở Rộng Lý Thuyết (Parallelism Bound)
Hệ số song song tối đa (Maximum Speedup Potential) theo định luật Brent:
$$\mathcal{P}_{\text{max}} = \frac{T_1}{T_\infty} = \mathcal{O}\left(\frac{m \log^{2/3} n}{2^{\log^{1/3} n} \log^2 n}\right) = \mathcal{O}\left(\frac{m}{2^{\log^{1/3} n} \log^{4/3} n}\right)$$
Với đồ thị có $n = 10^7, m = 10^8$:
$$\log_2 n \approx 23.25 \implies \log^{1/3} n \approx 2.85 \implies 2^{2.85} \approx 7.2$$
$$\mathcal{P}_{\text{max}} \approx \frac{10^8}{7.2 \times (23.25)^{4/3}} \approx \frac{10^8}{7.2 \times 65.6} \approx 211,000$$
Con số này chứng minh về mặt lý thuyết, thuật toán có dư địa song song cực lớn trên các đồ thị quy mô lớn.

---

## 6. So Sánh và Lựa Chọn Kiến Trúc Phần Cứng Mục Tiêu

| Tiêu chí kỹ thuật | CPU Đa Lõi (x86-64 OpenMP / TBB) | GPU Đồ Họa (NVIDIA CUDA / HIP) |
| :--- | :--- | :--- |
| **Độ phù hợp kiến trúc** | **RẤT CAO (Khuyến nghị)** | **THẤP / KHÔNG HIỆU QUẢ** |
| **Bản chất đệ quy** | Hỗ trợ hoàn hảo qua Call Stack hệ thống. | GPU bị tràn Call Stack khi đệ quy sâu và kích thước frontier không đều. |
| **Warp Divergence** | Không bị ảnh hưởng bởi rẽ nhánh logic phức tạp. | Rất nặng do các luồng trong cùng Warp rẽ nhánh khác nhau khi phân loại $K$ và $\mathcal{D}_1$. |
| **Kernel Launch Latency** | Overhead nhẹ ($\approx 1\,\mu\text{s}$ với OpenMP thread pool). | Độ trễ gọi kernel ($\approx 5 - 10\,\mu\text{s}$) triệt tiêu hiệu năng của các bước $k$ nhỏ ($k \approx 10-30$). |
| **Cơ chế đồng bộ bộ nhớ** | Hỗ trợ đầy đủ `cmpxchg16b` (128-bit atomic CAS), Cache Coherency phần cứng. | Atomic trên Global Memory có độ trễ cao; không hỗ trợ CAS 128-bit tùy biến tiện lợi như CPU. |
| **Đặc thù đồ thị** | Tối ưu tốt cho đồ thị thưa với bộ nhớ đệm L3 lớn (32MB - 128MB). | Bị nghẽn băng thông do truy cập ngẫu nhiên không liên tục (uncoalesced memory access). |

> [!TIP]
> **Định hướng phát triển:** Tập trung tối ưu hóa trên **CPU Multi-Core (4 – 64 luồng)** sử dụng C++20, thư viện OpenMP hoặc Intel OneAPI TBB với trình biên dịch `gcc`/`clang` bật cờ tối ưu hóa `-O3 -march=native -mcx16`.

---

## 7. Quy Trình Triển Khai Kiểm Thử Thực Nghiệm (Testing & Verification Plan)

Để đảm bảo thuật toán song song không sinh ra lỗi sai số hoặc race conditions:
1.  **Thread Sanitizer (TSan):** Biên dịch mã nguồn với cờ `-fsanitize=thread -g` để phát hiện toàn bộ các truy cập dữ liệu không an toàn (data races) trên mảng khoảng cách và các con trỏ block.
2.  **Đối chiếu kết quả (Ground Truth Verification):** Mọi kết quả khoảng cách $\hat{d}[v]$ xuất ra từ thuật toán BMSSP song song phải được so sánh khớp tuyệt đối ($|d_{\text{parallel}} - d_{\text{sequential}}| < 10^{-9}$) với thuật toán Dijkstra chuẩn trên $100\%$ các đỉnh có thể đến được.
3.  **Kiểm tra tính chịu tải đồng thời (Stress Test):** Chạy trên đồ thị ngẫu nhiên (Erdős–Rényi) và đồ thị mạng xã hội (Scale-Free Power-Law) với số lượng luồng tăng dần từ 1, 2, 4, 8, 16 đến 32 luồng để đo lường độ tăng tốc thực tế (Speedup) và hiệu suất song song (Parallel Efficiency).
