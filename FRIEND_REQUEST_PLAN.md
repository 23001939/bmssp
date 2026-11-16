# Kế hoạch đáp ứng yêu cầu: Source code BMSSP đúng + Benchmark hiệu năng

> Post-fix status (2026-11-14): scoped remediation is complete; see REMEDIATION_RESULTS.md at the workspace root for authoritative current status. Production near-tie path, frontier and isolated-node transform fixes passed regression; quick/full manifests passed 47 app/launcher + 10 native tests, and full checked 40 C++/Rust solver-cases. New timing evidence: BENCHMARK_V2_REPORT.md. Historical statements below describe their original snapshot. Full fidelity/theorem certification remains OPEN / NOT CERTIFIED.


Ngày tạo: 14/11/2026. Tác giả kế hoạch: AI assistant theo yêu cầu người dùng.

> **Đính chính theo review độc lập `bmssp_comparison/SIX_STAGES_REVIEW.md` (14/11/2026):** các dấu ✅, chữ “đóng”, “bit-exact”, “đã chứng minh” và tiến độ 100% bên dưới là ghi nhận lịch sử, không phải trạng thái nghiệm thu. Review tại thời điểm đó chạy 19 test app/API và 7 test native; các ca đạt nhưng benchmark lớn và toàn bộ Cargo tests chưa chạy lại. Sau review, root báo đã cập nhật solver/launcher, chạy 31 test và Chrome, đồng thời tạo benchmark v2; chi tiết xác nhận sau sửa sẽ được ghi riêng. O-3/O-4 (fidelity/cận) vẫn không thể đóng bằng test. Kết quả lịch sử được giữ với phạm vi đã chú giải.

## Bối cảnh

Bạn của người dùng muốn:

1. **Tìm source code** triển khai thuật toán BMSSP (Duan et al. 2025, arXiv:2504.17033v2)
2. **Thuật toán phải đúng** — cho kết quả chính xác trên mọi đầu vào hợp lệ
3. **Test hiệu năng** — so sánh BMSSP vs Dijkstra công bằng, trung thực

Bài báo chứng minh BMSSP có cận **O(m·log^(2/3) n)** theo giả thiết trong bài. Các benchmark lịch sử của repo cho thấy Dijkstra nhanh hơn ở nhiều ca đã đo; không đủ căn cứ để khẳng định điều đó cho mọi đầu vào hoặc quy nguyên nhân chỉ cho hằng số ẩn.

## Trạng thái hiện tại

### Các hạng mục được ghi nhận trong kế hoạch lịch sử

Các dòng bên dưới tóm tắt những gì hồ sơ cũ ghi đã làm; chúng không phải trạng thái nghiệm thu hiện tại và không thay cho điều kiện mở lại ở phần review.

| # | Hạng mục | Chi tiết |
|---|---|---|
| 1 | Khảo sát 6 repo GitHub | C++ (`lcs147`), Rust (`logtwothirds`), Python×2, C#, Go |
| 2 | Benchmark C++ và Python | 6 kích thước đồ thị (1.500→50.000 đỉnh) |
| 3 | Phát hiện repo sai | `bzantium V2` trả distance sai, có phản ví dụ |
| 4 | Fuzz test correctness | Hồ sơ cũ ghi 400 đồ thị khớp Bellman-Ford; cấu hình, exact/tolerance và kết quả chưa được review hiện tại tái lập |
| 5 | Visualizer hoạt động | Live trace BMSSP+Dijkstra, pause/step, xuất HTML |
| 6 | Upstream C++ tests | 8/8 test case, 345.272 assertions pass |
| 7 | **GĐ1: Rust toolchain** | Hồ sơ lịch sử ghi Cargo/rustc 1.100.0 và 42/42 test; review hiện tại không chạy lại toàn bộ Cargo tests. |
| 8 | **GĐ2: Cross-check C++ vs Rust** | Hồ sơ lịch sử ghi 30 đồ thị và 50 fuzz; review không tái chạy các tập này. Không diễn đạt là khớp bit-exact hay chứng minh tổng quát. |
| 9 | **GĐ3: Benchmark toàn diện** | Có kết quả lịch sử 18 đồ thị × 6 solver tại `full_results.json`; scope timer và khả năng so trực tiếp chưa được xác nhận đầy đủ. |
| 10 | **GĐ4: Audit fidelity sâu** | Có mapping và test lịch sử tại `bmssp_comparison/FIDELITY_AUDIT.md`; review tìm thấy lỗi predecessor và nghĩa vụ chứng minh còn mở. |
| 11 | **GĐ5: Transform chuẩn hóa bậc** | `n'`/`m'` trong báo cáo là ước lượng mô hình `2m`/`3m`, không phải kích thước thực từ từng solver. Tỷ số thời gian là lịch sử; nguyên nhân cache chưa đo. |
| 12 | **GĐ6: Đóng gói 1-click & Visualizer** | Demo và launcher có tồn tại, nhưng review tìm thấy launcher có thể báo PASS sai; hoàn thành demo không đồng nghĩa nghiệm thu kiểm chứng. |

### Trạng thái sau review độc lập (thay cho bảng đóng vấn đề cũ)

| # | Vấn đề | Mức độ | Trạng thái cuối cùng |
|---|---|---|---|
| O-1 | Predecessor/path sai trên near-tie | Cao | **Lỗi trong snapshot review đã được tái hiện** — follow-up code/tests được root báo sau review; xem xác nhận sau sửa. Đây không chứng nhận mọi phép toán double chính xác trên số thực. |
| O-2 | Constant-degree transformation | Cao | Báo cáo cũ dùng kích thước mô hình. Benchmark v2 hiện có số actual-size theo solver trên 30 cấu hình; xem `bmssp_comparison/BENCHMARK_V2_REPORT.md`. Kết luận giới hạn trong ma trận đã đo. |
| O-3 | Bất biến Algorithm 1–3 | Cao | **OPEN / NOT CERTIFIED** — vector-vs-set, root propagation, uniqueness và hợp đồng thủ tục còn cần chứng minh/kiểm tra. |
| O-4 | Cận amortized | Cao | **OPEN / NOT CERTIFIED** — randomized quickselect và chi phí vector shifting/remove(0) của Rust chưa được chứng minh có cận worst-case của bài. |
| O-6 | Timer scope không đồng nhất | Trung bình | Benchmark lịch sử có scope khác nhau; benchmark v2 mới tách setup/solve/input-to-result. Không gộp các phiên bản số liệu thành một bảng so sánh. |
| O-8 | Rust chưa chạy được | Trung bình | Toolchain/build/test được ghi nhận trong lịch sử; review mới không chạy full Cargo suite, nên chưa tái xác nhận. |
| O-10 | Benchmark chưa đa dạng | Trung bình | Có ma trận lịch sử 18 × 6; phương pháp luận chưa xác nhận lại và kết luận chỉ áp dụng cho các ca đo. |


---

## Giai đoạn 1: Cài đặt toolchain còn thiếu (ưu tiên CAO)

**Mục tiêu:** Build và chạy được repo chất lượng cao nhất.

| Bước | Việc cần làm | Thời gian |
|---|---|---|
| 1.1 | **Cài Rust + Cargo** | 30 phút |
| 1.2 | Build `logtwothirds` mainline: `cargo build --release` | 30 phút |
| 1.3 | Chạy test: `cargo test` + `cargo test --release --test variants_correctness` | 15 phút |
| 1.4 | Build Python wheel qua maturin: `maturin develop --release` | 15 phút |
| 1.5 | (Tùy chọn) Cài .NET 10 SDK cho C# repo | 30 phút |

**Tiêu chí xong:** `cargo test` pass, `shortest_paths(graph, source)` gọi được từ Python.
**Đóng vấn đề:** O-8

---

## Giai đoạn 2: Xác minh tính đúng (ưu tiên CAO)

**Mục tiêu:** Thu thập bằng chứng kiểm thử thực nghiệm cho ít nhất một implementation; test hữu hạn không phải chứng minh tổng quát.

| Bước | Việc cần làm |
|---|---|
| 2.1 | **Chạy differential test `logtwothirds`** — settlement-order exact vs Python reference |
| 2.2 | **Chạy property tests** — lưu rõ số đồ thị và tiêu chí exact hoặc tolerance khi so distances với Dijkstra |
| 2.3 | **Cross-check C++ vs Rust** trên ≥20 đồ thị chung |
| 2.4 | **Mở rộng fuzz C++** — n lên 1K–5K, thêm bậc lớn, trọng số 0, self-loop |
| 2.5 | **Sửa O-1** — giữ raw predecessor, tách verify exact vs epsilon |
| 2.6 | **Test constant-degree transform** — `prepare_graph(true)` vs oracle |

**Tiêu chí xong:** Differential + property test pass; C++ và Rust khớp trên ≥20 đồ thị.
**Đóng vấn đề:** O-1, O-2 (một phần), O-3 (một phần)

---

## Giai đoạn 3: Benchmark công bằng và toàn diện (ưu tiên CAO)

**Mục tiêu:** So sánh hiệu năng khoa học, tái lập được.

### 3A. Chuẩn hóa phương pháp đo

- Timer scope thống nhất: chỉ đo `execute()`, không tính build/addEdge/prepare
- ≥2 warm-up, ≥7 lần đo, lấy trung vị; seed cố định, chạy tuần tự

### 3B. Ma trận đồ thị

| Họ đồ thị | Kích thước |
|---|---|
| Random sparse (bậc 3–4) | 10K, 50K, 100K, 500K, 1M |
| Random dense (bậc ~√n) | 1K, 5K, 10K, 50K |
| Grid 2D | 100², 316², 1000² |
| Chain/Path | 10K, 100K, 1M |
| DIMACS road NY | 264K đỉnh, 730K cạnh |
| All-zero / Integer weights | 10K, 100K |

### 3C. Implementation cần đo

| Implementation | Ngôn ngữ |
|---|---|
| `lcs147` BMSSP WC+EXPECTED (có/không transform) | C++ |
| `lcs147` Dijkstra | C++ |
| `logtwothirds` Dijkstra / BMSSP / BMSSP-fast | Rust |
| `scipy.sparse.csgraph.dijkstra` | Python/C |

### 3D. Output cần xuất

- Bảng thời gian (ms) trung vị + tỷ số BMSSP/Dijkstra
- Biểu đồ log-log thời gian vs n
- File JSON tái lập

**Tiêu chí xong:** ≥5 họ × ≥4 size = ≥20 điểm đo; C++ và Rust; timer đồng nhất.
**Đóng vấn đề:** O-6, O-10

---

## Giai đoạn 4: Audit fidelity sâu (ưu tiên TRUNG BÌNH)

**Mục tiêu:** Đối chiếu chi tiết implementation với paper, ghi riêng quan sát code, test và phần chưa chứng minh.

| Bước | Việc cần làm |
|---|---|
| 4.1 | **Đọc `logtwothirds/ALGORITHM.md` + `AUDIT.md`** — đã audit line-by-line, 14 findings |
| 4.2 | **Đối chiếu C++ `lcs147` vs ALGORITHM.md** — mapping FindPivots, BaseCase, bmsspRec, batchPQ |
| 4.3 | **Kiểm tra 5 regression do dự án lựa chọn**; không gán chúng là danh sách §7 của PDF: settlement order, FindPivots cutoff/k-round, BatchPrepend vs Insert, partial-execution cutoff |
| 4.4 | **Bật `--features verify`** trên `logtwothirds` — invariant checks trên nhiều đồ thị |
| 4.5 | **Viết mapping table**: paper line → C++ line → Rust line |

**Tiêu chí xong:** Mapping table 3 cột hoàn chỉnh; 5 regression test pass; `--features verify` pass trên ≥100 đồ thị.
**Đóng vấn đề:** O-3, O-4 (một phần)

---

## Giai đoạn 5: Transform và miền áp dụng (ưu tiên TRUNG BÌNH)

| Bước | Việc cần làm |
|---|---|
| 5.1 | Bật `prepare_graph(true)` cho đồ thị bậc lớn |
| 5.2 | Đo chi phí transformation (n', m', thời gian) |
| 5.3 | So BMSSP có/không transform trên cùng đồ thị |
| 5.4 | Ghi rõ miền áp dụng trong tài liệu |

**Đóng vấn đề:** O-2

---

## Giai đoạn 6: Tài liệu và bàn giao (ưu tiên TRUNG BÌNH)

| Bước | Việc cần làm |
|---|---|
| 6.1 | Viết README tổng hợp — 1 file đủ thông tin |
| 6.2 | Sửa encoding `PROJECT_MEMORY.md` |
| 6.3 | Gộp 5 file tài liệu lặp thành 2 file |
| 6.4 | Viết script 1-click tái lập toàn bộ |
| 6.5 | Viết kết luận trung thực |

---

## Mốc tiến độ trong kế hoạch cũ (lịch sử, chưa phải nghiệm thu)

Các tổng phần trăm sau đây là thang tiến độ của kế hoạch cũ. Chúng không mô tả trạng thái remediation hiện tại; xem `REMEDIATION_PLAN.md`.

| Giai đoạn | Trọng số | Tổng khi xong | Phụ thuộc | Trạng thái |
|---|---:|---:|---|---|
| GĐ1: Cài toolchain | 15% | 15% | Không | Đã ghi hoàn tất trong hồ sơ cũ; review chưa chạy lại full Cargo tests |
| GĐ2: Xác minh đúng | 25% | 40% | GĐ1 | Đã có test lịch sử; không chứng minh đúng tổng quát |
| GĐ3: Benchmark toàn diện | 30% | 70% | GĐ1 | Có kết quả lịch sử; timer scope cần phân loại |
| GĐ4: Audit fidelity | 15% | 85% | GĐ1, GĐ2 | Mapping/test lịch sử; O-3/O-4 chưa chứng nhận |
| GĐ5: Transform | 10% | 95% | GĐ2, GĐ3 | Kích thước báo cáo là mô hình; cần đo actual |
| GĐ6: Tài liệu & 1-Click | 5% | 100% | Tất cả | Demo có; launcher chưa là cổng nghiệm thu đáng tin |

GĐ2 và GĐ3 có thể chạy song song sau khi GĐ1 xong.

## Ước tính thời gian

| Giai đoạn | Thời gian |
|---|---|
| GĐ1: Cài toolchain | 1–2 giờ |
| GĐ2: Xác minh đúng | 3–5 giờ |
| GĐ3: Benchmark | 4–8 giờ (phần lớn chờ chạy) |
| GĐ4: Audit | 4–6 giờ |
| GĐ5: Transform | 2–3 giờ |
| GĐ6: Tài liệu | 2–3 giờ |
| **Tổng** | **16–27 giờ làm việc** |

---

## Khuyến nghị repo

| Repo | Vai trò | Lý do |
|---|---|---|
| **`logtwothirds` (Rust)** | Ứng viên nghiên cứu/đối chiếu | Có ALGORITHM.md, AUDIT.md, test và benchmark theo hồ sơ repo. Randomized quickselect/vector costs chưa được chứng nhận worst-case như paper; kết quả đo cũ không chứng minh BMSSP thua Dijkstra ở mọi n khả thi. |
| **`lcs147/bmssp` (C++)** | 🥈 Phụ | Header-only, WC+Expected, Dijkstra built-in, benchmark tới 2^25 đỉnh |
| `nobelk` (Python) | Tham khảo | Đúng nhưng chậm |
| `bzantium` (Python) | ⛔ Không dùng | V2 sai kết quả |
| `mfreeman451` (Go) | ⛔ Không dùng | Thuật toán khác (delta-stepping) |

---

## Kết luận quan trọng (phải nói trước)

> **BMSSP là đột phá lý thuyết, không phải công cụ thực tế.**
>
> Bài báo chứng minh một cận tiệm cận cho BMSSP theo các giả thiết của bài. Không suy ra từ đó một kích thước crossover thực tế cụ thể. Kết quả lịch sử trong repo cho thấy Dijkstra nhanh hơn trên nhiều ca đo; cần xem raw results và phạm vi timer trước khi so sánh.
>
> Giá trị triển khai BMSSP: học thuật toán, kiểm thử implementation và khảo sát khoảng cách lý thuyết–thực hành; không dùng các benchmark nhỏ để xác nhận xu hướng tiệm cận.
>
> **Dùng Dijkstra làm baseline thực nghiệm; chọn solver theo workload và phép đo cùng phạm vi.**

---

## Tiêu chí đóng vấn đề (các mục vẫn đang mở theo review)

| Vấn đề | Đóng ở GĐ | Cách đóng |
|---|---|---|
| O-1: Predecessor sai | Mở | Sửa runner, exact/tolerance verifier và regression đường đi |
| O-2: Transform bị bỏ | Mở | Đo kích thước thực của từng solver, kiểm tra bảo toàn khoảng cách/ID |
| O-3: Bất biến chưa audit | Mở / NOT CERTIFIED | Mapping + bằng chứng các hợp đồng còn thiếu; test không thay chứng minh |
| O-4: Cận chưa chứng nhận | Mở / NOT CERTIFIED | Phân tích amortized/worst-case theo implementation |
| O-6: Timer không đồng nhất | Đã cải thiện trong benchmark v2 | Giữ legacy scopes riêng; chỉ so phép đo có cùng định nghĩa |
| O-8: Rust chưa chạy | GĐ1 | Cài Cargo + build |
| O-10: Benchmark hẹp | GĐ3 | Ma trận đồ thị mở rộng |

