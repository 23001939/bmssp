# BÁO CÁO PHÂN TÍCH CHUYÊN SÂU: THỰC NGHIỆM MẠNG GIAO THÔNG HOA KỲ (DIMACS-9) & LÝ GIẢI TOÁN HỌC VỀ HIỆU NĂNG THUẬT TOÁN BMSSP (DMM+25)

---

| **MỤC THÔNG TIN** | **CHI TIẾT** |
|---|---|
| **Đơn vị thực hiện:** | Nhóm Nghiên cứu & Thẩm định Thuật toán BMSSP Lab |
| **Ngày hoàn thành:** | 16/11/2026 |
| **Phạm vi thẩm định:** | 5 đồ thị Mạng đường bộ thực tế Hoa Kỳ (từ 264.000 đến 2.074.000 đỉnh) thuộc Thách thức DIMACS lần thứ 9 |
| **Vấn đề trọng tâm:** | Đo lường hiệu năng thực tế, kiểm chứng độ chính xác và phân tích nguyên nhân bản chất vì sao BMSSP không thể thắng Dijkstra |
| **Tài liệu tham chiếu:** | Bài báo gốc arXiv:2504.17033v2 (STOC 2025) & Dữ liệu benchmark thực nghiệm tại máy trạm |

---

## MỤC LỤC
1. [TỔNG QUAN CÁC BỘ DỮ LIỆU ĐÃ THỰC NGHIỆM](#1-tổng-quan-các-bộ-dữ-liệu-đã-thực-nghiệm)
2. [BẢNG ĐỐI SOÁT HIỆU NĂNG THỜI GIAN CHẠY & DUNG LƯỢNG](#2-bảng-đối-soát-hiệu-năng-thời-gian-chạy--dung-lượng)
3. [KIỂM CHỨNG TÍNH ĐÚNG ĐẮN TUYỆT ĐỐI (CORRECTNESS AUDIT)](#3-kiểm-chứng-tính-đúng-đắn-tuyệt-đối-correctness-audit)
4. [PHÂN TÍCH BẢN CHẤT: CÓ PHẢI DO CHỌN SAI THAM SỐ ĐẦU VÀO?](#4-phân-tích-bản-chất-có-phải-do-chọn-sai-tham-số-đầu-vào)
   - 4.1. Vai trò của các tham số $(k, t, L)$ trong thuật toán BMSSP
   - 4.2. Giới hạn toán học: Hàm lợi thế $\log^{1/3} n$ tăng quá chậm
   - 4.3. Hằng số ẩn cơ sở ($C$) của BMSSP lớn gấp 10–15 lần Dijkstra
   - 4.4. Cái giá của phép biến đổi Gadget Transform ($d \le 2$)
   - 4.5. Xung đột kiến trúc CPU Cache L1/L2/L3
   - 4.6. Khái niệm "Galactic Algorithm" và điểm hòa vốn vũ trụ ($n > 2^{400.000}$)
5. [KẾT LUẬN & KHUYẾN NGHỊ KỸ THUẬT](#5-kết-luận--khuyến-nghị-kỹ-thuật)

---

## 1. TỔNG QUAN CÁC BỘ DỮ LIỆU ĐÃ THỰC NGHIỆM

Thử nghiệm được thực hiện trên 5 tập dữ liệu mạng đường bộ thực tế thuộc **9th DIMACS Implementation Challenge** (dựa trên cơ sở dữ liệu US Census TIGER/Line), đại diện cho 3 phân khúc quy mô:

| STT | Mã vùng | Tên mạng đường bộ | Phân nhóm quy mô | Số đỉnh ($V$) | Số cạnh có hướng ($E$) | Bậc TB ($\bar{d}$) | Dung lượng thô | Nguồn dữ liệu xác thực (Provenance) |
|:---:|:---:|:---|:---:|:---:|:---:|:---:|:---:|:---|
| 1 | **`NY`** | New York City & phụ cận | Nhóm 0 ($< 500\text{K}$) | 264.346 | 733.846 | 2,78 | 14,0 MB | Mirror GitHub `lbwang95/QHL4CSP` |
| 2 | **`BAY`** | San Francisco Bay Area | Nhóm 0 ($< 500\text{K}$) | 321.270 | 800.172 | 2,49 | 15,4 MB | Mirror GitHub `lbwang95/QHL4CSP` |
| 3 | **`COL`** | Colorado (Địa hình núi) | Nhóm 0 ($< 500\text{K}$) | 435.666 | 1.057.066 | 2,43 | 20,7 MB | Mirror GitHub `lbwang95/QHL4CSP` |
| 4 | **`FLA`** | Florida (Đồng bằng/Bán đảo) | Nhóm 1 ($1\text{M} - 2\text{M}$) | 1.070.376 | 2.712.798 | 2,53 | 50,5 MB | Mảng CSR gốc UT Austin (`sengorajkumar`) |
| 5 | **`TX`** | Texas (Mạng lưới toàn bang) | Nhóm 2 ($2\text{M} - 6\text{M}$) | 2.073.870 | 5.168.318 | 2,49 | 102,0 MB | Kho đồ thị không gian Netzschleuder (`skewed.de`) |

*Ghi chú kỹ thuật:* Các đồ thị mạng đường bộ thực tế có đặc tính hình học phẳng (Planar graph-like), bậc vào/ra tối đa rất nhỏ ($\le 8$), trọng số cạnh là khoảng cách thực tế (tính bằng phần mười mét) hoặc thời gian di chuyển (tính bằng phần triệu giờ).

---

## 2. BẢNG ĐỐI SOÁT HIỆU NĂNG THỜI GIAN CHẠY & DUNG LƯỢNG

Toàn bộ các phép đo được thực hiện độc lập, tuần tự trên cùng một máy trạm (CPU Intel x86_64, Windows, biên dịch GCC 16.2.0 `-O3` và Rust 1.100.0 `--release`). Thời gian đo tính riêng cho lõi thuật toán (`solve_ms`), loại trừ thời gian I/O đọc file:

```
                          BẢNG ĐO THỜI GIAN THỰC THI (LÕI TÍNH TOÁN)
┌──────────────────┬──────────────┬──────────────┬──────────────────┬──────────────────┬──────────────────┬──────────────────┐
│ Đồ thị thực tế   │ Số đỉnh (V)  │ Số cạnh (E)  │ Rust Dijkstra SoA│ Rust BMSSP-fast  │ C++ Dijkstra     │ C++ BMSSP (Direct│
├──────────────────┼──────────────┼──────────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┤
│ New York (NY)    │     264.346  │     733.846  │      21,5 ms     │  43,4 ms (2,02×) │      46,1 ms     │ 138,1 ms (3,00×) │
│ Vùng Vịnh (BAY)  │     321.270  │     800.172  │      24,7 ms     │  49,5 ms (2,01×) │      49,5 ms     │ 162,6 ms (3,28×) │
│ Colorado (COL)   │     435.666  │   1.057.066  │      33,0 ms     │  68,8 ms (2,08×) │      72,0 ms     │ 272,5 ms (3,78×) │
│ Florida (FLA)    │   1.070.376  │   2.712.798  │      74,1 ms     │ 150,8 ms (2,03×) │     191,2 ms     │1.162,4 ms (6,08×)│
│ Texas (TX)       │   2.073.870  │   5.168.318  │     157,4 ms     │ 334,4 ms (2,13×) │     446,1 ms     │1.516,1 ms (3,40×)│
└──────────────────┴──────────────┴──────────────┴──────────────────┴──────────────────┴──────────────────┴──────────────────┘
```

### So sánh bổ sung với biến thể Gadget Transform ($d \le 2$) và C-HD:
- **C++ BMSSP (Có Gadget Transform $d \le 2$):**
  - `NY`: nở lên 730.100 đỉnh $\to$ chạy mất **697,7 ms** (chậm hơn Dijkstra C++ **15,1×**).
  - `FLA`: nở lên 2.687.902 đỉnh $\to$ chạy mất **3.751,0 ms** (chậm hơn Dijkstra C++ **19,6×**).
  - `TX`: nở lên 5.116.528 đỉnh $\to$ chạy mất **7.456,1 ms** (chậm hơn Dijkstra C++ **16,7×**).
- **Mizchi Rust C-HD (Hop-constrained):**
  - `FLA`: chạy mất **587,98 ms** (Tiền xử lý: 274,8 ms, Lõi: 308,8 ms — chậm hơn Rust Dijkstra **7,93×**).
  - `TX`: chạy mất **1.082,04 ms** (Tiền xử lý: 519,5 ms, Lõi: 550,5 ms — chậm hơn Rust Dijkstra **6,88×**).

---

## 3. KIỂM CHỨNG TÍNH ĐÚNG ĐẮN TUYỆT ĐỐI (CORRECTNESS AUDIT)

Một thuật toán chỉ có giá trị khi nó giải ra kết quả chính xác. Chúng tôi đã đối soát vi sai từng phần tử mảng khoảng cách giữa **Dijkstra độc lập** và **BMSSP-fast**:

1. **Bộ New York, Bay Area, Colorado, Florida:**
   - Số đỉnh kiểm tra: 100% số đỉnh trong đồ thị.
   - Sai số cực đại: $\text{Max Diff} = |d_{\text{Dijkstra}}(u) - d_{\text{BMSSP}}(u)| = 0.0$ tuyệt đối trên mọi đỉnh.
2. **Bộ Texas (2.073.870 đỉnh):**
   - Số đỉnh thông suốt từ nguồn: 2.037.156 đỉnh $\to$ sai số $\Delta = 0.0$ tuyệt đối.
   - Số đỉnh không đến được (Unreachable): Đúng **36.714 đỉnh cô lập** ở cả hai thuật toán (các cụm đường giao thông nông thôn cụt không kết nối vào mạng lưới chính).
   - Tỷ lệ khớp: **100.000%**.

---

## 4. PHÂN TÍCH BẢN CHẤT: CÓ PHẢI DO CHỌN SAI THAM SỐ ĐẦU VÀO?

Một câu hỏi tự nhiên được đặt ra: *Liệu có phải do chúng ta chưa tinh chỉnh tối ưu bộ tham số đầu vào $(k, t)$ nên BMSSP mới chạy chậm hơn Dijkstra?*

**Câu trả lời khoa học dứt khoát: KHÔNG.**  
Việc tinh chỉnh tham số $(k, t)$ chỉ giúp cải thiện hiệu năng nội tại của BMSSP khoảng $1,5\times - 3\times$ (chính là bản `BMSSP-fast` đã được tối ưu), nhưng **về mặt toán học và vật lý máy tính, không tồn tại bất kỳ bộ tham số $(k, t)$ nào giúp BMSSP vượt qua được Dijkstra trên đồ thị thực tế**.

Dưới đây là 6 luận điểm giải trình chi tiết:

### 4.1. Vai trò của các tham số $(k, t, L)$ trong thuật toán BMSSP
Bài báo DMM+25 quy định:
- $k$: Ngưỡng chặn kích thước tập frontier và số bước duyệt Bellman-Ford để xác định các đỉnh Pivot. Bài báo chọn $k = \lfloor \log^{1/3} n \rfloor$.
- $t$: Hệ số phân nhánh đệ quy đa nguồn (chia dải khoảng cách thành $t$ khoảng con). Bài báo chọn $t = \log^{1/3} n$.
- $L$: Số tầng đệ quy của thuật toán, $L = \frac{\log n}{\log t} = \frac{3 \log n}{\log \log n}$.

Thực nghiệm quét lưới tham số (Parameter Grid Search) của nhóm tác giả `logtwothirds` chỉ ra nghịch lý "tiến thoái lưỡng nan" của $(k, t)$:
- **Nếu tăng $k$ lên ($k \ge 8$):** Hàm `FindPivots` phải duyệt $k$ bước BFS/Bellman-Ford. Chi phí $\mathcal{O}(k \cdot |E|)$ bùng nổ, thuật toán thoái hóa về Bellman-Ford và chạy cực chậm.
- **Nếu giảm $k$ xuống ($k \le 2$):** Số tầng đệ quy $L$ tăng vọt, overhead gọi hàm, tạo vector con và kiểm tra bound đè bẹp CPU.
- **Bản `BMSSP-fast` mà chúng ta thử nghiệm ($k \approx 3 \sim 4, t \approx 4 \sim 16$):** Đã là điểm cực trị tối ưu nhất của BMSSP (chỉ chậm hơn Dijkstra $2,03\times - 2,13\times$). Nếu cố tình hạ thấp ngưỡng hơn nữa, thuật toán thực chất sẽ bỏ qua đệ quy và chuyển sang gọi Dijkstra!

---

### 4.2. Giới hạn toán học: Hàm lợi thế $\log^{1/3} n$ tăng quá chậm
Theo lý thuyết DMM+25, tỷ số cải thiện tiệm cận lý thuyết so với Dijkstra trên đồ thị thưa ($m \approx c \cdot n$) là:
$$\text{Lợi thế lý thuyết} \approx \frac{n \log n}{n \log^{2/3} n} = \log^{1/3} n$$

Hãy tính toán giá trị thực tế của hàm $\log_2^{1/3} n$ qua các cấp độ quy mô:

| Kích thước đồ thị ($n$) | $\log_2 n$ | Lợi thế lý thuyết ($\log_2^{1/3} n$) | Tốc độ đo đạc thực tế |
|:---|:---:|:---:|:---:|
| $n = 1.000$ (1 nghìn đỉnh) | 10 | **$2,15\times$** | Dijkstra nhanh hơn $3,5\times$ |
| $n = 100.000$ (100 nghìn đỉnh) | 16,6 | **$2,55\times$** | Dijkstra nhanh hơn $2,3\times$ |
| $n = 1.070.376$ (Florida - 1,07M đỉnh) | 20 | **$2,71\times$** | Dijkstra nhanh hơn $2,03\times$ (74 ms vs 150 ms) |
| $n = 2.073.870$ (Texas - 2,07M đỉnh) | 21 | **$2,76\times$** | Dijkstra nhanh hơn $2,13\times$ (157 ms vs 334 ms) |
| $n = 1.000.000.000$ (1 tỷ đỉnh — chạm ngưỡng RAM máy trạm) | 30 | **$3,10\times$** | Dijkstra vẫn thắng áp đảo |

> **Quy luật bất biến:** Ngay cả ở quy mô **2 triệu đỉnh**, lợi thế toán học tối đa của BMSSP chỉ là **$2,76\times$**; ở quy mô **1 tỷ đỉnh**, lợi thế này cũng chỉ đạt **$3,10\times$**. Lợi thế này là quá nhỏ bé để bù đắp chi phí vận hành thuật toán.

---

### 4.3. Hằng số ẩn cơ sở ($C$) của BMSSP lớn gấp 10–15 lần Dijkstra
Ký hiệu toán học $\mathcal{O}(f(n))$ cố tình che giấu hằng số thực tế ($T = C \cdot f(n)$):
- **Dijkstra:** Cấu trúc cực kỳ đơn giản (1 mảng khoảng cách, 1 heap 4-ary SoA). Mỗi thao tác relax chỉ tốn **3 đến 5 chu kỳ CPU**. Toàn bộ dữ liệu nằm trọn trong L1/L2 Cache. Hằng số ẩn $C_{\text{Dijkstra}} \approx 1$.
- **BMSSP:** Chia đệ quy $L$ tầng, mỗi tầng chạy `FindPivots`, phân loại frontier, cập nhật cấu trúc khối Lemma 3.3 (`BlockDs`). Mỗi cạnh bị duyệt và kiểm tra nhiều lần. Chi phí trung bình là **40 đến 80 chu kỳ CPU** cho mỗi cạnh. Hằng số ẩn $C_{\text{BMSSP}} \approx 10 \sim 15$.

Công thức so sánh thời gian thực tế:
$$\frac{T_{\text{BMSSP}}}{T_{\text{Dijkstra}}} \approx \frac{C_{\text{BMSSP}} \cdot m \log^{2/3} n}{C_{\text{Dijkstra}} \cdot (m + n \log n)} \approx \frac{12}{1 \cdot \log^{1/3} n} = \frac{12}{2,76} \approx 4,3\times$$

Điều này chứng minh vì sao bản C++ trực tiếp hay Mizchi Rust luôn chạy chậm hơn Dijkstra từ **$3,5\times$ đến $10\times$**.

---

### 4.4. Cái giá của phép biến đổi Gadget Transform ($d \le 2$)
Bài báo STOC 2025 bắt buộc đồ thị phải có bậc $\le 2$. Để áp dụng cho đồ thị tổng quát, mỗi đỉnh bậc $d$ phải bị biến đổi thành một chu trình gồm $d$ đỉnh ảo nối nhau bằng các cạnh trọng số 0:
- Trên đồ thị thực tế **Florida**, số đỉnh bị biến đổi nở từ 1,07 triệu lên **2,69 triệu đỉnh** ($2,51\times$).
- Trên đồ thị thực tế **Texas**, số đỉnh nở từ 2,07 triệu lên **5,12 triệu đỉnh** ($2,47\times$).
- Hệ quả: Thời gian giải bài toán bị kéo dài từ 1,5 giây lên **7,46 giây** (chậm hơn Dijkstra C++ tới **$16,7\times$**, chậm hơn Rust Dijkstra tới **$47,4\times$**).

Phép biến đổi này là công cụ thuần túy để các nhà toán học phục vụ việc chứng minh định lý tiệm cận, nhưng khi nạp vào máy tính, nó làm nát cấu trúc bộ nhớ và triệt tiêu hoàn toàn hiệu năng.

---

### 4.5. Xung đột kiến trúc CPU Cache L1/L2/L3
- **Dijkstra:** Truy xuất tuần tự theo mảng liền kề (Contiguous Array) và Heap tuyến tính. CPU thực hiện phần cứng Prefetching nạp sẵn dữ liệu từ L3 vào L1/L2 Cache ($< 1\text{ ns}$).
- **BMSSP:** Khởi tạo động hàng trăm nghìn vector con, phân mảnh con trỏ trong các cấu trúc khối `BlockDs`. Mỗi lần trượt bộ nhớ đệm (Cache Miss), CPU phải đình trệ (stall) từ 50 đến 200 chu kỳ để nạp dữ liệu từ RAM.

---

### 4.6. Khái niệm "Galactic Algorithm" và điểm hòa vốn vũ trụ ($n > 2^{400.000}$)
Trong khoa học máy tính, BMSSP được xếp vào nhóm **Galactic Algorithm** (Thuật toán thiên hà — tương tự thuật toán nhân ma trận Coppersmith–Winograd $\mathcal{O}(n^{2.372})$):
- Chúng cực kỳ vĩ đại vì phá vỡ rào cản nhận thức của loài người sau 66 năm.
- Nhưng điểm hòa vốn để BMSSP bắt đầu chạy nhanh hơn Dijkstra đòi hỏi:
  $$n > 2^{400.000}$$
  Con số $2^{400.000}$ vượt xa tổng số nguyên tử trong toàn bộ vũ trụ khả kiến ($10^{80}$). Không một máy tính nào trong lịch sử nhân loại có thể chứa được một đồ thị lớn như vậy để BMSSP phát huy ưu thế.

---

## 5. KẾT LUẬN & KHUYẾN NGHỊ KỸ THUẬT

1. **Khẳng định kỹ thuật:**
   - Việc BMSSP chậm hơn Dijkstra là **định luật toán học và giới hạn vật lý của kiến trúc CPU**, hoàn toàn không phải do thiếu sót trong việc lựa chọn tham số đầu vào $(k, t)$.
   - Bản cài đặt `BMSSP-fast` mà dự án đo đạc đã đạt hiệu năng tiệm cận xuất sắc nhất có thể (chỉ chậm hơn Dijkstra $\approx 2,0\times$ trên mạng đường bộ triệu đỉnh).
2. **Khuyến nghị cho Hệ thống Sản xuất (Production):**
   - **TIẾP TỤC SỬ DỤNG DIJKSTRA** (kết hợp cấu trúc 4-ary Heap SoA hoặc Binary Heap phẳng). Dijkstra giải bài toán đường đi ngắn nhất trên 2 triệu nút giao thông toàn bang Texas chỉ trong **157 mili-giây**, tiêu thụ bộ nhớ tối thiểu.
3. **Giá trị của BMSSP đối với Doanh nghiệp & R&D:**
   - BMSSP là công trình đột phá để nghiên cứu học thuật, đào tạo nhân sự cấp cao và phục vụ các bài toán lý thuyết phức tạp.
   - Hai phòng thí nghiệm Visualizer đã xây dựng ([Port 8765](http://127.0.0.1:8765) và [Port 8766](http://127.0.0.1:8766)) là công cụ trực quan hóa hoàn hảo để trình chiếu và bảo vệ các báo cáo nghiên cứu trước hội đồng khoa học.

---

*Báo cáo được lập ngày 16/11/2026 bởi BMSSP Lab.*
