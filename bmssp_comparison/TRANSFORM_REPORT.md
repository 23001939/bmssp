# BÁO CÁO GIAI ĐOẠN 5: KHẢO SÁT CHUẨN HÓA BẬC HẰNG SỐ (ISSUE O-2)

> Post-fix status (2026-11-14): scoped remediation is complete; see REMEDIATION_RESULTS.md at the workspace root for authoritative current status. Production near-tie path, frontier and isolated-node transform fixes passed regression; quick/full manifests passed 47 app/launcher + 10 native tests, and full checked 40 C++/Rust solver-cases. New timing evidence: BENCHMARK_V2_REPORT.md. Historical statements below describe their original snapshot. Full fidelity/theorem certification remains OPEN / NOT CERTIFIED.


**Ngày thực hiện:** 14/11/2026
**Mục tiêu:** Đo lường tác động thực tế của phép biến đổi đồ thị về bậc hằng số (in/out-degree <= 2) theo Section 2 của bài báo gốc arXiv:2504.17033v2.

> **Đính chính review độc lập `SIX_STAGES_REVIEW.md` (14/11/2026):** bảng này là kết quả lịch sử, không phải phép đo kích thước đồ thị sau transform. `compute_expansion_metrics()` trong script tạo `n'=2m`, `m'=3m` theo mô hình endpoint; các giá trị dưới đây không được đọc từ cấu trúc đã transform của solver. Trường `accuracy` trong JSON chỉ có nghĩa sai số không vượt `1e-7`, không phải bit-exact. Thời gian là số lịch sử; scope chính xác và khả năng quy chênh lệch riêng cho transform chưa được xác nhận. Không có hardware counter, nên cache là giả thuyết, không phải nguyên nhân đã đo. Không dùng báo cáo này để đóng O-2/O-4.

`BENCHMARK_V2_REPORT.md` là báo cáo mới hơn cho phạm vi đã đo: có kích thước thực sau prepare và timer được tách pha. Bảng dưới đây được giữ làm dữ liệu legacy để truy vết, không gộp với v2.

---

## 1. Cơ sở lý thuyết của bài báo (Lemma 2.1 & Section 2)

Thuật toán BMSSP được chứng minh dưới giả định đồ thị đầu vào $G$ có **bậc vào và bậc ra tối đa là hằng số** (cụ thể $\le 2$).
- Để áp dụng cho đồ thị tổng quát với bậc tùy ý, bài báo áp dụng kỹ thuật kinh điển: mỗi đỉnh $v$ có bậc $d_v$ được thay thế bằng một chu trình có hướng gồm $d_v$ đỉnh ảo (stand-in nodes) nối với nhau bằng các cạnh trọng số $0$.
- Mỗi cạnh tới nối vào một đỉnh ảo riêng, và mỗi cạnh ra rời đi từ một đỉnh ảo riêng.
- Theo phép biến đổi toán học trong bài, khoảng cách giữa đỉnh đại diện được thiết kế để bảo toàn; báo cáo benchmark này không tự kiểm tra cấu trúc/ánh xạ thực của từng solver.
- Về mặt tiệm cận lý thuyết: $|V'| = O(m)$ và $|E'| = O(m)$. Vì $m \le n^2$, độ phức tạp tiệm cận lý thuyết $O(m \log^{2/3} n)$ vẫn được bảo toàn.

## 2. Bảng thời gian lịch sử và kích thước transform theo mô hình

Các cột $(n',m')$ và tỷ số được tính từ `2m`/`3m`, không phải thống kê đồ thị thực sau transform. Thời gian lấy từ `transform_results.json`; script đọc `median_ms` nếu có, nếu không dùng trung vị `samples_ms`, rồi fallback sang wall time. Scope nội tại của những trường thời gian này cần đối chiếu từng binary/wrapper; giữ ở đây để truy vết, không so trực tiếp như benchmark đã chuẩn hóa.

| Tên đồ thị | Đồ thị gốc $(n, m)$ | Ước lượng mô hình $(2m,3m)$ | Mô hình $2m/n$ | C++ Dijkstra | C++ BMSSP mode `wc` | C++ BMSSP mode `wc_bounded` | Rust Dijkstra | Rust BMSSP | Rust BMSSP-fast |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Star Hub d=100** | (101, 200) | (400, 600) | **3.96×** | 0.01ms | **0.33ms** | 0.21ms | 0.00ms | **0.15ms** | 0.00ms |
| **Star Hub d=500** | (501, 1000) | (2000, 3000) | **3.99×** | 0.02ms | **0.78ms** | 0.74ms | 0.01ms | **1.04ms** | 0.02ms |
| **Star Hub d=2000** | (2001, 4000) | (8000, 12000) | **4.0×** | 0.20ms | **3.04ms** | 3.98ms | 0.04ms | **5.64ms** | 0.13ms |
| **Dense n=500 avg_d=20** | (500, 10000) | (20000, 30000) | **40.0×** | 0.12ms | **27.40ms** | 1.21ms | 0.03ms | **13.22ms** | 0.06ms |
| **Dense n=1000 avg_d=40** | (1000, 40000) | (80000, 120000) | **80.0×** | 0.55ms | **163.19ms** | 4.27ms | 0.13ms | **119.37ms** | 0.55ms |
| **Scale-Free BA n=1000 m_att=5** | (1000, 9970) | (19940, 29910) | **19.94×** | 0.24ms | **17.33ms** | 1.65ms | 0.05ms | **15.95ms** | 0.12ms |
| **Scale-Free BA n=3000 m_att=8** | (3000, 47928) | (95856, 143784) | **31.95×** | 1.14ms | **92.80ms** | 6.63ms | 0.29ms | **93.27ms** | 0.68ms |
| **Sparse n=2000 avg_d=3** | (2000, 6000) | (12000, 18000) | **6.0×** | 0.22ms | **15.87ms** | 1.71ms | 0.07ms | **7.86ms** | 0.18ms |
| **Sparse n=10000 avg_d=4** | (10000, 40000) | (80000, 120000) | **8.0×** | 2.47ms | **145.40ms** | 10.21ms | 0.56ms | **67.56ms** | 1.34ms |

*Ý nghĩa chính xác của cột `accuracy` trong `transform_results.json`: mọi khoảng cách trong ca đó nằm trong dung sai tuyệt đối `1e-7` so với Dijkstra Python. Đây không phải exact/bitwise match, cũng không kiểm tra predecessor/path. Hơn nữa cùng một floating-point oracle không chứng minh đúng trên mọi đầu vào.*

---

## 3. Diễn giải thận trọng; nguyên nhân phần cứng chưa đo

Các thời gian lịch sử gợi ý transform có thể làm tăng chi phí trên một số cấu hình. Dữ liệu hiện có không chứng minh nguyên nhân phần cứng cụ thể:

### 3.1. Bùng nổ kích thước bộ nhớ & Phá vỡ CPU Cache
- Mô hình endpoint cho `Dense n=1000,m=40000` cho $n'=80000$; đây không phải kích thước C++ transform thực tế đã xác nhận. Review code chỉ ra C++ gộp láng giềng hai chiều bằng map và thêm sentinel, vì vậy kích thước thực có thể khác đáng kể.
- Chưa đo footprint thực của từng solver, cache miss, băng thông hay độ trễ bộ nhớ. Các phát biểu về L1/L2/L3/DRAM, số ns và hệ số chậm do cache ở bản cũ được rút lại, không coi là kết quả.

### 3.2. Hiện tượng 'Hub Serialization' trên mạng Scale-Free / Star
- Các mô tả “4,000 đỉnh ảo” dựa trên công thức endpoint của script, không xác nhận layout do code C++ tạo ra.
- Hub serialization là giả thuyết có thể khảo sát bằng trace/counter; báo cáo hiện tại không đo số relax hoặc thao tác trên đỉnh ảo để xác nhận nguyên nhân.

### 3.3. So sánh có Transform vs Không Transform (Rust & C++)
- **C++:** Cột `wc` và `wc_bounded` trong bảng là các lệnh/biến thể khác nhau ngoài việc bật/tắt transform; con số của chúng không cô lập riêng transform.
- **Rust:** `bmssp` và `bmssp-fast` cũng thay đổi nhiều yếu tố cùng lúc. Tỷ số cũ không thể quy riêng cho transform.

---

## 4. Miền áp dụng — chưa kết luận O-2

1. **Về mặt lý thuyết:** Paper nêu giả thiết bậc phù hợp và phép biến đổi để áp dụng kết quả cho đồ thị tổng quát. Cách từng implementation thực hiện và ánh xạ phép biến đổi cần được kiểm tra riêng.
2. **Về mặt thực nghiệm:** Bảng lịch sử không cô lập riêng transform và không có actual-size/cache counters; chưa đủ để nói transform làm chậm mọi đồ thị hub hoặc xác định miền thắng.
3. **Miền áp dụng:** Cần đo nhiều topology/size/seed và xác minh kết quả cùng đường đi. Chưa xác định miền mà BMSSP cạnh tranh với Dijkstra.

**Trạng thái của báo cáo v1:** không đủ để đóng O-2. Benchmark v2 sau đó bổ sung actual-size và đo bật/tắt transform trong cùng họ C++ đã nêu ở `BENCHMARK_V2_REPORT.md`; kết luận chỉ giới hạn trong 30 cấu hình/seed và scope của báo cáo mới. Không có hardware counters nên nguyên nhân cache vẫn chưa xác nhận; O-4 vẫn OPEN / NOT CERTIFIED.
