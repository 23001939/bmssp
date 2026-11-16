# Tài Liệu Thiết Kế: Song Song Hóa Thuật Toán BMSSP (Bounded Multi-Source Shortest Path)

Tài liệu này tổng hợp toàn bộ các phân tích kỹ thuật, phương pháp xác định vùng song song, nút thắt cổ chai, cùng với thiết kế chi tiết cho cấu trúc dữ liệu cấu hình đồng thời (`Concurrent Data Structure D`) phục vụ cho dự án song song hóa thuật toán **BMSSP** (được giới thiệu trong bài báo *"Breaking the Sorting Barrier for Directed Single-Source Shortest Paths"*).

---

## 1. Mô Hình Song Song Phù Hợp

Việc chuyển đổi thuật toán BMSSP sang tính toán song song phù hợp nhất với mô hình **Song song kết hợp giữa Tác vụ (Task Parallelism) và Dữ liệu/Đồ thị (Data/Graph Parallelism)**:

*   **Song song tác vụ (Task Parallelism):** Áp dụng trên cấu trúc chia để trị (divide-and-conquer) theo các tầng đệ quy $\lceil \log n / t \rceil$ và các lời gọi đệ quy độc lập của hàm `BMSSP` trên các tập đỉnh con khác nhau.
*   **Song song dữ liệu/đồ thị (Data/Graph Parallelism):** Phù hợp cho các giai đoạn xử lý mảng đỉnh/cạnh lớn như vòng lặp relax cạnh kiểu Bellman-Ford trong hàm `FINDPIVOTS` hoặc quá trình duyệt và cập nhật các đỉnh lân cận trong đồ thị.

---

## 2. Các Phần Có Thể Song Song Hóa Trong Thuật Toán

Các thành phần cốt lõi trong thuật toán BMSSP có tiềm năng song song hóa cao bao gồm:

1.  **Hàm `FINDPIVOTS` (Thuật toán 1):**
    *   *Vòng lặp relax $k$ bước (Dòng 4–11):* Quá trình duyệt qua các cạnh $(u, v)$ với $u \in W_{i-1}$ để cập nhật khoảng cách $\hat{d}[v]$ hoàn toàn độc lập giữa các đỉnh trong cùng một tầng.
    *   *Xây dựng rừng và tìm gốc (Dòng 15–16):* Việc xác định các cây trong đồ thị có hướng $F$ và tìm các nút gốc có kích thước $\ge k$ có thể tận dụng các thuật toán đồ thị song song.
2.  **Giai đoạn Relax cạnh trong hàm `BMSSP` (Thuật toán 3):**
    *   Sau khi nhận được tập đỉnh hoàn thành $U_i$ từ lời gọi đệ quy, việc duyệt qua tất cả các cạnh xuất phát từ $u \in U_i$ (Dòng 14–20) để cập nhật khoảng cách tới các đỉnh kề $v$ có thể thực hiện song song trên nhiều luồng.
3.  **Cấu trúc dữ liệu $D$ (Block-based linked list - Lemma 3.3):**
    *   Các thao tác xử lý trên các block độc lập (như tìm kiếm nhị phân trên cây tìm kiếm cân bằng quản lý các upper bound của block, hoặc thao tác chia tách block - `SPLIT`) có thể được tối ưu hóa bằng các cấu trúc dữ liệu song song.

---

## 3. Nút Thắt Cổ Chai (Bottlenecks) và Thách Thức Kỹ Thuật

Khi thiết kế thuật toán song song cho BMSSP, bạn cần đặc biệt lưu ý các điểm nghẽn sau:

*   **Sự phụ thuộc thứ tự tuần tự giữa các iteration ($i$) trong hàm `BMSSP`:** Vòng lặp theo biến $i$ có tính phụ thuộc dữ liệu chặt chẽ (lần lặp sau cần kết quả $B_i$, $S_i$ và trạng thái mới nhất của cấu trúc dữ liệu $D$ từ lần lặp trước).
*   **Tranh chấp dữ liệu (Race Conditions) khi Relax cạnh:** Nhiều luồng cùng thực hiện việc cập nhật giá trị khoảng cách $\hat{d}[v] \leftarrow \min(\hat{d}[v], \hat{d}[u] + w_{uv})$ cho cùng một đỉnh $v$ đích có thể dẫn đến xung đột ghi (cần dùng lệnh nguyên tử CAS).
*   **Chi phí quản lý và tranh chấp trên cấu trúc dữ liệu $D$:** Thao tác `Insert`, `Batch Prepend`, và `Pull` diễn ra liên tục. Tranh chấp khóa (lock contention) khi thêm/xóa phần tử vào các block hoặc tái cân bằng cây chỉ mục có thể triệt tiêu lợi thế song song.
*   **Độ trễ truyền dữ liệu và tính cục bộ bộ nhớ (Locality):** Thuật toán đồ thị thưa có độ trễ truy cập bộ nhớ bất định, dễ dẫn đến tình trạng nghẽn băng thông bộ nhớ (memory bandwidth bound).

---

## 4. Thiết Kế Cấu Trúc Dữ Liệu $D$ Dạng Khóa Song Song (Lock-Free / Fine-Grained Locking)

Để đảm bảo hiệu năng cao trong môi trường đa luồng, cấu trúc dữ liệu phân tầng $D$ ($\mathcal{D}_0$ và $\mathcal{D}_1$) được thiết kế tối ưu như sau:

### 4.1. Tách Biệt Kiến Trúc $\mathcal{D}_0$ và $\mathcal{D}_1$
*   **$\mathcal{D}_0$ (Lock-Free Treiber Stack):** Dành riêng cho `BATCHPREPEND` (thêm các batch từ kết quả partial execution hoặc pivots). $\mathcal{D}_0$ được xây dựng như một danh sách liên kết đơn hoặc Stack sử dụng lệnh nguyên tử `CAS` (Compare-And-Swap), giúp các luồng thực hiện prepend đồng thời mà **không cần khóa (lock-free)**.
*   **$\mathcal{D}_1$ (Fine-Grained / Concurrent Tree + Block Locks):** Quản lý các thao tác `INSERT` thông thường với yêu cầu duy trì thứ tự giá trị giữa các block.

### 4.2. Chi Tiết Thiết Kế $\mathcal{D}_1$
*   **Thay thế Red-Black Tree bằng Concurrent Skip List / B-Link Tree:** Dùng để quản lý các *upper bound* của các block. Điều này giúp các luồng định vị block cần chèn thông qua duyệt con trỏ đồng thời mà không phải khóa toàn bộ cây.
*   **Khóa cấp độ Block (Block-Level Spinlocks):** Mỗi block trong $\mathcal{D}_1$ (chứa tối đa $M$ phần tử dạng danh sách liên kết) được trang bị một spinlock riêng (`std::atomic_flag`). Khi chèn cặp $\langle v, \hat{d}[v] \rangle$, luồng **chỉ cần khóa duy nhất block đó**, cho phép các luồng khác thao tác trên các block khác song song.

### 4.3. Xử Lý Thao Tác Chia Tách Block (`SPLIT`) Không Nghẽn
Khi một block vượt quá kích thước giới hạn $M$:
1. Luồng phát hiện trạng thái tràn sẽ chuyển trạng thái block sang chế độ **"Splitting"** thông qua biến atomic.
2. Các luồng khác nếu muốn truy cập block này sẽ chờ nhẹ hoặc chuyển hướng tạm thời, trong khi các block khác vẫn hoạt động bình thường.
3. Quá trình tìm median và chia đôi danh sách thực hiện cục bộ trên bộ nhớ riêng của luồng, sau đó cập nhật lại cây chỉ mục upper bound bằng giao dịch nhỏ hoặc CAS.

---

## 5. Sơ Đồ Kiến Trúc Luồng Dữ Liệu (Data-Flow Diagram) Cho Thao Tác Chèn Đồng Thời

```text
[ Yêu cầu Chèn Dữ liệu / Prepend ]
          │
          ├──> [ 1. Phân loại thao tác ]
          │         │
          │         ├─► Loại A: BATCHPREPEND (Batch từ K hoặc tập Pivots S_i)
          │         │         │
          │         │         └──► [ D_0: Lock-Free Treiber Stack / Linked List ]
          │         │                   └── Thêm block mới vào đầu danh sách thông qua lệnh nguyên tử CAS.
          │         │                   └── Hoàn toàn không khóa (Lock-free), loại bỏ hoàn toàn tranh chấp luồng.
          │         │
          │         └─► Loại B: INSERT thông thường (Cạnh (u, v) được relax)
                    │         │
                    │         └──► [ D_1: Concurrent Skip List / B-Link Tree (Quản lý Upper Bound) ]
                    │                   │
                    │                   └──► Định vị Block tương ứng qua chỉ mục
                    │                             │
                    │                             └──► [ Khóa cấp độ Block (Block-Level Spinlock) ]
                    │                                       │
                    │                                       └──► Chèn cặp khóa/giá trị vào Linked List của Block
                    │                                                 │
                    │                                                 ├── Kích thước <= M: Hoàn tất chèn
                    │                                                 └── Kích thước > M: Kích hoạt tiến trình [ SPLIT ] 
                    │                                                           (Tách block cục bộ, cập nhật cây chỉ mục)
```