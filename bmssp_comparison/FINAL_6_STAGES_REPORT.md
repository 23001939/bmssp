# BÁO CÁO LỊCH SỬ 6 GIAI ĐOẠN BMSSP — ĐÍNH CHÍNH THEO REVIEW

> Post-fix status (2026-11-14): scoped remediation is complete; see REMEDIATION_RESULTS.md at the workspace root for authoritative current status. Production near-tie path, frontier and isolated-node transform fixes passed regression; quick/full manifests passed 47 app/launcher + 10 native tests, and full checked 40 C++/Rust solver-cases. New timing evidence: BENCHMARK_V2_REPORT.md. Historical statements below describe their original snapshot. Full fidelity/theorem certification remains OPEN / NOT CERTIFIED.


**Dự án:** Nghiên cứu, Xác minh Thực nghiệm và Tinh chỉnh Thuật toán BMSSP ($O(m \log^{2/3} n)$ — Ran Duan et al., arXiv:2504.17033v2)  
**Ngày hoàn tất:** 14/11/2026  
**Thực hiện:** Main Agent (Gemini) phối hợp hệ thống công cụ kiểm chứng tự động  
**Trạng thái của báo cáo này:** **Không chứng minh nghiệm thu 100%.** Các kết quả dưới đây là số liệu/báo cáo lịch sử. Review độc lập `SIX_STAGES_REVIEW.md` (14/11/2026) chạy 19 test app/API và 7 test native đạt, không chạy benchmark lớn/full Cargo, và tái hiện lỗi predecessor cùng các kết luận quá mức về fidelity, timer và transform. Sau review, root báo đã cập nhật solver/launcher, chạy 31 test và Chrome, đồng thời tạo benchmark v2; xem phần “sau sửa” do root bổ sung ở đầu báo cáo này. Những thay đổi đó không chứng nhận cận lý thuyết.

> Các số liệu thời gian cũ được giữ để truy vết, nhưng chưa tái xác nhận scope đo tương đương. “PASS” test hữu hạn không phải chứng minh chính xác tổng quát. Tham chiếu `REMEDIATION_PLAN.md` cho tiêu chí khắc phục.

---

## 1. TÓM TẮT ĐIỀU HÀNH (EXECUTIVE SUMMARY)

Đề tài nghiên cứu thuật toán Bounded Multi-Source Shortest Path (BMSSP) của Ran Duan et al. công bố vào tháng 04/2025 là một đột phá lý thuyết lớn trong khoa học máy tính lý thuyết, phá vỡ ranh giới $O(m + n \log n)$ của Dijkstra tồn tại suốt 66 năm (từ 1959).

Tuy nhiên, khi tiếp cận mã nguồn thực tế trên thế giới, hệ thống ban đầu gặp hàng loạt trở ngại nghiêm trọng:
1. Bản Rust (`logtwothirds`) — repo trung thực nhất với bài báo — không thể biên dịch trên môi trường Windows do lỗi liên kết thư viện GCC (`libgcc_eh.a`).
2. Một ghi chú cũ nêu tỷ số BMSSP/Dijkstra 1.4×–2.8× nhưng không dẫn nguồn cụ thể; không dùng đây như phát biểu đã xác minh của tác giả repo. Các wrapper benchmark có timer scope khác nhau, cần mô tả theo từng phép đo.
3. Lỗi làm tròn số thực `SCALE = 1e10` ở C++ và BFS $10^{-9}$ khiến thuật toán chọn sai đường đi tối ưu ở các ca near-tie (vấn đề O-1).
4. Phép biến đổi đồ thị về bậc hằng số (Section 2 của paper) bị bỏ qua hoặc chưa từng được đo lường tác động phần cứng (vấn đề O-2).
5. Chưa có bất kỳ kiểm toán dòng-dòng (line-by-line audit) nào chứng minh mã nguồn thực sự tuân thủ các bất biến của Paper thay vì vô tình thoái hóa thành Dijkstra (vấn đề O-3).

Hồ sơ lịch sử ghi nhận sáu nhóm công việc gồm toolchain, kiểm thử, benchmark, mapping fidelity, khảo sát transform và visualizer. Review sau đó cho thấy một số xác nhận trong hồ sơ không được bằng chứng hỗ trợ; bảng dưới đây là lịch sử, không phải trạng thái nghiệm thu.

---

## 2. TỔNG QUAN 6 GIAI ĐOẠN TINH CHỈNH

| Giai đoạn | Trọng số | Trạng thái | Nhiệm vụ trọng tâm & Kết quả cốt lõi |
| :--- | :---: | :---: | :--- |
| **GĐ1: Cài đặt Toolchain & Build Rust** | 15% | Đã ghi nhận trong lịch sử | Hồ sơ ghi Rust GNU toolchain 1.100.0 và 42/42 tests. Review hiện tại không chạy lại toàn bộ Cargo tests; O-8 chưa tái xác nhận trong review này. |
| **GĐ2: Xác minh đúng đắn & Vi sai** | 25% | Đã có test lịch sử; không chứng nhận | Báo cáo ghi 30 đồ thị + 50 fuzz. Review không tái chạy tập này; cụm “khớp chính xác” không được dùng nếu tiêu chí là dung sai. |
| **GĐ3: Benchmark Toàn diện** | 30% | Có kết quả lịch sử; scope cần phân loại | Ma trận 18 × 6 có trong `full_results.json`; review phát hiện cross-check lấy `samples_ms` thay `kernel_ms`, Rust timer bao gồm `sssp_bmssp` với transform. Không gọi đây là solver-only đồng nhất. |
| **GĐ4: Audit Fidelity Sâu** | 15% | Mapping lịch sử; OPEN / NOT CERTIFIED | Năm kiểm tra là do dự án lựa chọn, không phải regression tests liệt kê ở §7 PDF. Mapping/invariants không chứng minh fidelity hoặc cận. |
| **GĐ5: Transform Chuẩn hóa Bậc** | 10% | Thăm dò lịch sử; OPEN | $n'=2m,m'=3m$ là mô hình endpoint trong script, không phải số đo transform thực của solver. Cache chưa đo; so sánh các biến thể đổi nhiều yếu tố. |
| **GĐ6: Đóng gói 1-Click & Visualizer** | 5% | Demo có; nghiệm thu launcher OPEN | Review phát hiện lỗi luồng PASS/FAIL trong `run_all.py`; không dùng báo cáo cũ để khẳng định cổng kiểm tra đáng tin. |
| **TỔNG TIẾN ĐỘ** | **100%** | **Không xác nhận hoàn tất** | **Các mục đang khắc phục được theo dõi riêng trong `REMEDIATION_PLAN.md`.** |


---

## 3. BẢNG ĐỐI CHIẾU HỆ THỐNG: TRƯỚC (BEFORE) VS SAU (AFTER) 6 GIAI ĐOẠN

Bảng dưới đây tổng kết sự thay đổi mang tính bản chất của toàn bộ dự án trên mọi phương diện kỹ thuật:

| Tiêu chí so sánh | TRƯỚC 6 GIAI ĐOẠN (Hệ thống Ban đầu) | SAU 6 GIAI ĐOẠN (Hệ thống Tinh chỉnh) | Ý nghĩa kỹ thuật & Giá trị đạt được |
| :--- | :--- | :--- | :--- |
| **1. Khả năng hỗ trợ Ngôn ngữ & Toolchain** | - Chỉ chạy được C++ (`lcs147`).<br>- Rust (`logtwothirds`) bị tê liệt hoàn toàn do lỗi linker GCC `cannot find -lgcc_eh`.<br>- Python variant bị lỗi logic hoặc chậm gấp hàng nghìn lần. | - Cài đặt hoàn chỉnh Rust GNU Toolchain 1.100.0.<br>- Khắc phục xung đột `w64devkit` bằng stub archive `libgcc_eh.a`.<br>- Build release nhị phân C++ và Rust native hoạt động hoàn hảo.<br>- **42/42 tests Rust PASS**. | **Đóng hoàn toàn O-8**. Mở khóa khả năng đối chiếu chéo (differential testing) giữa hai hệ sinh thái mạnh nhất là C++ và Rust. |
| **2. Độ tin cậy về tính đúng & số học** | C++ upstream có rounding; runner visualizer tại snapshot review dùng BFS với tolerance $10^{-9}$ và chọn predecessor không tối ưu chính xác trong near-tie. | Rust `f64::total_cmp` định thứ tự khóa; không biến phép cộng float thành số thực chính xác. Ca kiểm thử hữu hạn vẫn không chứng nhận mọi đầu vào. | **O-1 OPEN:** đường đi và khoảng cách phải có trạng thái kiểm chứng riêng; không tuyên bố đúng tuyệt đối. |
| **3. Phương pháp đo benchmark** | Các wrapper khác nhau về setup, transform, solver và đầu ra. | Có ma trận lịch sử 18 × 6 nhưng không đủ bằng chứng để gọi mọi số là `execute()`/solver-only tương đương. | **O-6 OPEN:** phải phân loại theo raw timer; các số cũ không phải bằng chứng cho scope chuẩn hóa. |
| **4. Fidelity lý thuyết** | Có các câu hỏi chưa xác minh về set/vector semantics, root propagation, queue và amortization. | Có mapping bảng và test hữu hạn. §7 “What this is not” không phải tiêu đề trong PDF; Rust quickselect/vector operations còn vấn đề worst-case. | **O-3/O-4 OPEN / NOT CERTIFIED.** |
| **5. Degree transformation** | Một số benchmark gọi solver có/không transform. | Script tính $n'=2m,m'=3m$ theo mô hình, không lấy actual graph stats; so sánh các biến thể đồng thời thay đổi nhiều yếu tố. | **O-2 OPEN:** chưa quy kết được tỷ số cho riêng transform hoặc cho cache. |
| **6. Giao diện Trực quan & Tự động hóa** | - Mã nguồn rời rạc, không có giao diện tương tác trực tiếp cho người dùng.<br>- Người dùng phải gõ lệnh thủ công phức tạp trong terminal. | - Hệ thống Visualizer Web cục bộ hoàn chỉnh (port 8765) trên đồ thị `weighted_100`.<br>- Tích hợp hai nút Chạy BMSSP và Chạy Dijkstra song song.<br>- Cung cấp script master `run_all.py` 1-click tự động chạy kiểm thử và tự động mở tab trình duyệt cho người dùng. | Mang lại trải nghiệm hoàn hảo, trực quan, minh bạch cho người dùng sau khi hoàn thành toàn bộ công việc. |

---

## 4. CHI TIẾT KỸ THUẬT CÁC PHÁT HIỆN ĐỘT PHÁ

### 4.1. Kết quả đo lịch sử và giới hạn ngoại suy
* **Lý thuyết:** BMSSP đạt độ phức tạp $O(m \log^{2/3} n)$ tốt hơn $O(m + n \log n)$ của Dijkstra khi $n \to \infty$.
* **Thực tế:** Hằng số ẩn của BMSSP là cực kỳ lớn:
  - Cấu trúc dữ liệu phân khối 2 tầng `BlockDS` (Quickselect phân vị trung vị, tái cân bằng danh sách).
  - Thuật toán `FindPivots` (duyệt $k$ vòng Bellman-Ford, xây dựng rừng cạnh căng, đếm kích thước cây con).
  - Đệ quy $L = \lceil \frac{\log n}{t} \rceil$ tầng với hàng nghìn cuộc gọi hàm mini-Dijkstra `BaseCase`.
* **Kết quả lịch sử:**
  - Báo cáo cũ ghi ở $n=50,000$: Rust Dijkstra **4.19 ms**, BMSSP mainline **425.4 ms**, và `bmssp-fast` **9.45 ms**. Review hiện tại không chạy lại benchmark lớn; xem `full_results.json`, cấu hình và timer trước khi so trực tiếp.
  - Không suy ra điểm crossover từ các hằng số của vài cấu hình. Ước lượng $2^{400,000}$ trong bản cũ không có mô hình ngoại suy đủ để được dùng như dự đoán.

### 4.2. Transform: số lịch sử và giới hạn diễn giải (Issue O-2)
Trong `TRANSFORM_REPORT.md`, $n'=80,000$ với đồ thị dày là kết quả công thức mô hình `n'=2m`; script không đọc kích thước thực sau `prepare_graph(true)`. Review code cho thấy C++ gộp láng giềng bằng map và thêm sentinel, nên không được trình bày con số mô hình là kích thước thực solver. Các phát biểu về tràn cache, độ trễ L1/DRAM và cache miss chưa được đo bằng hardware counters. 163.19 ms và 4.27 ms là số lịch sử từ hai mode/biến thể; scope chưa được xác nhận là tương đương và không cô lập transform.

---

## 5. HƯỚNG DẪN TÁI LẬP 1-CLICK (REPRODUCTION INSTRUCTIONS)

Tất cả các kết quả nghiên cứu, benchmark và giao diện demo được tích hợp vào script master `run_all.py` tại thư mục gốc:

```powershell
# Chạy toàn bộ pipeline kiểm chứng tự động và tự động mở tab demo visualizer:
python run_all.py

# Chế độ chạy nhanh (< 15 giây) và mở giao diện demo:
python run_all.py --quick

# Mở ngay giao diện demo trực tiếp:
python run_all.py --demo-only
```

Đây là mô tả launcher trong hồ sơ cũ, không phải xác nhận hiện hành: review phát hiện `run_all.py` có thể báo PASS khi thiếu script hoặc kết quả fail. Hãy dùng `REMEDIATION_PLAN.md` và trạng thái của launcher sau khi sửa trước khi dựa vào lệnh này làm cổng nghiệm thu. Visualizer là demo tương tác; không nên gọi hai lần chạy là benchmark công bằng nếu chưa kiểm soát điều kiện đo.

---

## 6. KẾT LUẬN

Tại thời điểm review, O-1, O-2 và O-6 cần khắc phục/đo lại; các follow-up solver/launcher và benchmark v2 được ghi riêng ở phần cập nhật sau sửa. O-3/O-4 vẫn OPEN / NOT CERTIFIED: test và mapping không chứng nhận fidelity/cận. Kết quả BMSSP chậm trên các ca đã benchmark không phủ định cận lý thuyết; cận tiệm cận cũng không dự đoán solver nhanh hơn trên mọi kích thước hoặc họ đồ thị.


