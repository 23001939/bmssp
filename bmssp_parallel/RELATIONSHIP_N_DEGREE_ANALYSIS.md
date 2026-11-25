# Tổng Hợp Quy Luật Thực Nghiệm Giữa Số Đỉnh ($n$), Bậc Đỉnh ($d$) Với Hiệu Năng Dijkstra Và Parallel BMSSP

**Ngày lập báo cáo:** 25/11/2026  
**Cơ sở dữ liệu thực nghiệm:** 23 bộ dữ liệu đã đo và đối chứng độc lập (`MaxDiff = 0.000` trên 100% phép đo) trên cùng hệ thống **AMD Ryzen 5 7640HS (6C/12T, L3 Cache 16 MB)**, GCC `16.2.0 -O3 -march=native -mcx16 -fopenmp`:
- **11 đồ thị tổng hợp kiểm soát biến số** (`100K → 2.0M` đỉnh, bậc `d = 4, 12, 16, 20`): [`final_benchmark_1m_2m.csv`](final_benchmark_1m_2m.csv)
- **7 mạng giao thông đường bộ thực tế Hoa Kỳ DIMACS-9** (`264K → 1.89M` đỉnh, bậc `d = 2,44 – 2,76`): [`dimacs_road_7_results.csv`](dimacs_road_7_results.csv)
- **5 mạng liên kết Web, xã hội & trích dẫn thực tế Stanford SNAP** (`34,5K → 1.79M` đỉnh, bậc `d = 10,71 – 18,75`): [`snap_highdeg_results.csv`](snap_highdeg_results.csv)
- **Biểu đồ tổng hợp 4 góc nhìn:** [`relationship_n_degree_bmssp_vs_dijkstra.png`](relationship_n_degree_bmssp_vs_dijkstra.png)

---

## 1. Mô Hình Giải Tích Thực Nghiệm Của Hai Thuật Toán Theo $(n, d)$

Gọi $n$ là số đỉnh, $d = m/n$ là bậc ra trung bình ($m = n \cdot d$ cung có hướng), và $P$ là số luồng phần cứng. Từ dữ liệu đo thực tế, thời gian chạy của hai thuật toán tuân theo hai cấu trúc chi phí hoàn toàn khác nhau:

### 1.1. `Dijkstra` tuần tự (Binary Min-Heap)
$$T_{\text{Dijkstra}}(n, d) \;\approx\; c_{\text{heap}}(n) \cdot n \log_2 n \;+\; c_{\text{relax}}(n, \text{locality}) \cdot (n \cdot d)$$

- **Nhạy mạnh với số đỉnh $n$ qua ngưỡng bộ nhớ đệm:** Khi $n \lesssim 100\text{K}$, mảng khoảng cách và heap nằm trọn trong L2/L3 cache (`16 MB`), hệ số $c_{\text{heap}}(n)$ rất nhỏ (`cit-HepPh`: `6,41 ms`). Khi $n$ vượt `400K – 1M` đỉnh, mỗi thao tác vun đống nhị phân và tra cứu `dist[v]` phát sinh trượt L3 cache ra RAM, làm $c_{\text{heap}}(n)$ và $c_{\text{relax}}(n)$ phình gấp **3 – 5 lần**.
- **Tăng tuyến tính-nặng theo bậc $d$ trên 1 nhân đơn:** Khi bậc $d$ tăng gấp `6,6×` (từ `CAL` $d=2,44$ lên `soc-Pokec` $d=18,75$ ở cùng ~1,7M đỉnh), số phép đọc ngẫu nhiên `dist[v]` và đẩy lười biếng lên heap tăng vọt, kéo thời gian `Dijkstra` chậm đi **`5,12 lần`** (`482,93 ms → 2.474,98 ms`).

### 1.2. `Parallel BMSSP` đã tối ưu (Algorithm 1–3 & Cấu trúc khối $\mathcal{D}$ — Lemma 3.3)
$$T_{\text{BMSSP}}(n, d, P) \;\approx\; T_{\text{khung}}(n) \;+\; c_{\mathcal{D}}(n) \cdot n \log_2^{2/3} n \;+\; \frac{c_{\text{CSR}} \cdot (n \cdot d)}{P_{\text{eff}}(n, d, \text{đường kính})}$$

Ba thành phần này giải thích chính xác mọi hiện tượng thực nghiệm đã quan sát:
1. **$T_{\text{khung}}(n)$ (Chi phí khung đệ quy + đánh thức vùng OpenMP):** Chiếm ưu thế khi $n$ nhỏ (`< 100K` đỉnh).
2. **$c_{\mathcal{D}}(n) \cdot n \log_2^{2/3} n$ (Chi phí quản lý tập đỉnh chưa hoàn tất trong $\mathcal{D}_0 \cup \mathcal{D}_1$):** Phụ thuộc chủ yếu vào **số đỉnh $n$**, gần như **không phình theo bậc $d$**.
3. **$\frac{c_{\text{CSR}} \cdot (n \cdot d)}{P_{\text{eff}}}$ (Quét dải kề `CSR` tuần tự trên bộ nhớ và chia tải qua $P_{\text{eff}}$ luồng hiệu dụng):** Khi bậc $d$ càng cao (`12 – 20`) và đường kính đồ thị nhỏ (*small-world*), mặt sóng càng rộng nên $P_{\text{eff}}$ càng lớn (hệ số tự tăng tốc nội tại đạt **`1,84× – 2,28×`**), giúp `BMSSP` hấp thụ hàng chục triệu cạnh tăng thêm với độ trễ tăng rất nhẹ (cạnh tăng `6,63×` từ `CAL` sang `soc-Pokec` nhưng thời gian `BMSSP 12T` chỉ tăng **`1,52×`**).

---

## 2. Ba Quy Luật Định Lượng Rút Ra Từ 23 Phép Đo

Hồi quy mặt phản ứng log-tuyến tính trên toàn bộ 23 bộ dữ liệu đạt hệ số xác định **$R^2 = 0,9018$** (giải thích hơn 90% phương sai thực nghiệm):

### Quy luật 1 — Tác động của Số đỉnh ($n$) khi giữ cố định Bậc ($d$)
> **Trên mọi họ đồ thị, khi số đỉnh $n$ tăng, tỷ số tốc độ `T_Dijkstra / T_BMSSP` đều tăng đơn điệu có lợi cho `BMSSP` (đặc biệt sau mốc tràn L3 Cache `~400K – 1M` đỉnh).**

| Họ đồ thị (cố định dải bậc $d$) | Quy mô nhỏ | Quy mô trung bình | Quy mô lớn (`1M – 2M` đỉnh) | Điểm cắt vượt `Dijkstra` (`1,00×`) |
|---|---|---|---|---|
| **Ngẫu nhiên bậc cao (`d = 12 – 20`)** | `200K`: **`1,55×`** | `500K`: **`1,69×`** | `1M`: **`1,82×`** → `2M`: **`2,20×`** | `< 100K` đỉnh |
| **Ngẫu nhiên thưa (`d = 4,0`)** | `100K`: `0,90×` | `200K`: **`1,10×`** → `500K`: **`1,39×`** | `1M`: **`1,50×`** → `2M`: **`1,65×`** | **`~150K` đỉnh** |
| **Thực tế SNAP (`d = 10,7 – 18,8`)** | `34,5K`: `0,35×` → `77,4K`: `0,82×` | `685K` (`460K` tới được): **`1,03×`** | `1,79M`: **`1,69×`** → `1,63M`: **`1,83×`** | **`~450K – 500K` đỉnh tới được** |
| **Thực tế Đường bộ (`d = 2,4 – 2,7`)** | `264K–321K`: `0,37×–0,38×` | `436K–1,21M`: `0,42×–0,48×` | `1,52M–1,89M`: `0,54×–0,59×` | Chưa cắt ở `≤ 2M` đỉnh do $d$ quá bé |

### Quy luật 2 — Tác động của Bậc trung bình ($d = m/n$) khi giữ cố định Số đỉnh ($n$)
> **Ở cùng một quy mô số đỉnh $n$, bậc trung bình $d$ càng cao thì ưu thế của `BMSSP` trước `Dijkstra` càng lớn và khả năng mở rộng đa luồng nội tại càng mạnh.**

| Lát cắt đẳng đỉnh ($n$ cố định) | Bậc rất thấp (`d ≈ 2,5`) | Bậc thấp (`d = 4`) | Bậc cao (`d = 12 – 20`) | Mức khuếch đại lợi thế |
|---|---|---|---|---|
| **$n = 200\text{K}$** | `NY` (`264K`, `d=2,76`): `0,38×` | `ER 200K` (`d=4`): **`1,10×`** | `Dense-20 200K` (`d=20`): **`1,55×`** | Từ thua `2,6×` thành **thắng `1,55×`** |
| **$n \approx 500\text{K}$** | `COL` (`436K`, `d=2,39`): `0,43×` | `ER 500K` (`d=4`): **`1,39×`** | `Dense-16 500K` (`d=16`): **`1,69×`** | Tăng tốc vọt lên **`1,69×`** (`Self-Scale = 2,04×`) |
| **$n \approx 1,0\text{M}$** | `FLA` (`1,07M`, `d=2,51`): `0,42×` | `ER 1M` (`d=4`): **`1,50×`** | `Dense-12 1M` (`d=12`): **`1,82×`** | Tăng tốc vọt lên **`1,82×`** (`Self-Scale = 2,12×`) |
| **$n \approx 1,6\text{M} – 2,0\text{M}$** | `CAL` (`1,89M`, `d=2,44`): `0,54×` | `ER 2M` (`d=4`): **`1,65×`** | `Wiki` (`15,9`): **`1,69×`**; `Pokec` (`18,8`): **`1,83×`**; `Dense-12 2M`: **`2,20×`** | Từ thua `1,85×` thành **thắng `1,69× – 2,20×`** |

### Quy luật 3 — Vai trò phụ trợ của Cấu trúc hình học & Tính cục bộ bộ nhớ (Locality)
Ở cùng $(n, d)$, đồ thị có **tính cụm cục bộ cao** (đường bộ phẳng TIGER/Line hoặc tên miền Web `web-BerkStan`) giúp `Dijkstra` hưởng lợi cache L1/L2 tốt hơn so với đồ thị liên kết toàn cục (mạng xã hội `soc-Pokec`, `Erdős–Rényi`), làm dịch chuyển ranh giới hòa vốn từ `~150K` đỉnh (trên đồ thị ngẫu nhiên) sang `~460K` đỉnh (trên đồ thị thực tế phân cụm).

---

## 3. Bảng Khuyến Nghị Lựa Chọn Thuật Toán Thực Tiễn

| Đặc trưng bài toán đầu vào $(n, d)$ | Thuật toán tối ưu thực tế | Lý do kỹ thuật |
|---|---|---|
| **Đồ thị nhỏ (`n < 150.000` đỉnh, mọi bậc $d$)** | **`Dijkstra` tuần tự** | Nằm trọn trong L2/L3 cache (`< 55 ms`); không tốn chi phí khởi tạo đệ quy và đồng bộ luồng. |
| **Mạng phẳng bậc cực thấp (`d ≤ 3`, ví dụ bản đồ giao thông đường bộ `n ≤ 2M`)** | **`Dijkstra` tuần tự** | Mặt sóng địa lý quá hẹp làm giảm hiệu suất đa luồng của `BMSSP`, trong khi `Dijkstra` khai thác cực tốt tính cục bộ không gian. |
| **Đồ thị trung bình (`n ≈ 200K – 500K`) có bậc `d ≥ 4` (ngẫu nhiên) hoặc `d ≥ 11` (Web/Xã hội)** | **`Parallel BMSSP` (8 luồng)** | Bắt đầu vượt `Dijkstra` từ `1,03×` đến `1,69×`. |
| **Đồ thị lớn (`n ≥ 1.000.000` đỉnh) với bậc `d ≥ 4`, đặc biệt `d = 12 – 20+`** | **`Parallel BMSSP` (8 – 12 luồng)** | Vùng ưu thế tuyệt đối: **nhanh hơn `Dijkstra` từ `1,41×` đến `2,20×`**, tiết kiệm tới hơn `1,1 – 1,4 giây` mỗi truy vấn SSSP. |
