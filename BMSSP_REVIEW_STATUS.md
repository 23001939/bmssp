# Trạng thái review BMSSP — session closeout

**Ngày chốt:** 14/11/2026 (Asia/Saigon)  
**Trạng thái chung:** **CÒN VẤN ĐỀ CHƯA GIẢI QUYẾT — không được coi là reference implementation đã chứng nhận.**

Tài liệu này chốt kết quả review code, PDF và test của session hiện tại. Nó không sửa solver, không thay đổi benchmark lịch sử và không thay thế chứng minh hình thức.

## 1. Kết luận hiện tại

Visualizer có **lõi BMSSP thật**, không phải Dijkstra đổi tên:

- Runner BMSSP dùng bản sao instrumented của `lcs147/bmssp`, commit `0a8eb82805bad7f64dfa1b51e17656c9af4c4681`.
- Mã có các thành phần tương ứng `FindPivots` (Algorithm 1), `BaseCase` (Algorithm 2), `BMSSP` đệ quy (Algorithm 3) và `batchPQ` (Lemma 3.3).
- Runner BMSSP không có Dijkstra fallback.
- Trace phát từ các bước thực của thuật toán (đệ quy, pivot, relax, complete), không dựng animation từ kết quả cuối.

Tuy nhiên, **không đủ cơ sở** để tuyên bố bản hiện tại:

1. trung thực hoàn toàn với paper ở mọi chi tiết;
2. đúng trên mọi input;
3. thỏa/được chứng nhận cận `O(m log^(2/3) n)` cho arbitrary input; hoặc
4. nhanh hơn Dijkstra trong thực tế.

Định vị chính xác ở thời điểm chốt:

> Prototype nghiên cứu/giảng dạy: tái hiện và trực quan hóa BMSSP, kiểm thử thực nghiệm với oracle độc lập, đồng thời khảo sát trung thực khoảng cách giữa cận lý thuyết và hiệu năng thực tế.

## 2. Nguồn đối chiếu

- Paper gốc: `C:\ttss\2504.17033v2.pdf`, Duan, Mao, Mao, Shu, Yin, *Breaking the Sorting Barrier for Directed Single-Source Shortest Paths*, arXiv:2504.17033v2.
  - Abstract khẳng định thuật toán deterministic `O(m log^(2/3) n)` trong comparison-addition model cho đồ thị có hướng với trọng số thực không âm.
  - Đây là định lý về thuật toán; **không tự chứng nhận implementation hiện tại**.
- Upstream C++: `C:\ttss\bmssp_comparison\repos\cpp\`, `lcs147/bmssp`, commit nêu trên.
- Demo fork: `C:\ttss\bmssp_visualizer\native\bmssp-traced.hpp` và `runner.cpp`.
- Review trước: `C:\ttss\bmssp_visualizer\FIDELITY_REVIEW.md`.
- Audit hiệu năng: `C:\ttss\bmssp_visualizer\PERFORMANCE_AUDIT.md`.

## 3. Vấn đề còn mở — CHƯA SỬA

| ID | Mức độ | Vấn đề | Bằng chứng hiện có | Điều kiện đóng vấn đề |
|---|---|---|---|---|
| O-1 | Cao | **Predecessor/path xuất ra không chính xác tuyệt đối trên near-tie.** Runner bỏ `rawpred`, dựng lại BFS theo cạnh “tight” với tolerance `1e-9`. | `C:\ttss\bmssp_visualizer\exports\fidelity-near-tie.json`: input `0→1=4e-11`, `0→2=0`, `2→1=0`; distance `[0,0,0]` đúng, nhưng path xuất 0→1 có giá `4e-11`, thay vì 0→2→1 giá 0. Verifier vẫn `ok=true` do cũng dùng tolerance. | Giữ/kiểm raw predecessor theo total-order label hoặc dựng path với tiêu chuẩn exact đã công bố; tách verification exact khỏi epsilon verification; regression phải bắt ca này. |
| O-2 | Cao | **Demo luôn dùng `prepare_graph(false)`.** Không thực hiện constant-degree transformation cho đồ thị import tùy ý. | `C:\ttss\bmssp_visualizer\native\runner.cpp` gọi `prepare_graph(false)`. Paper đặt thuật toán trên đồ thị sau transformation có in/out-degree ≤2. | Bật/chọn transformation đúng, ánh xạ ID transformed↔original trong output/trace/UI, hoặc giới hạn input đã được kiểm tra bậc và không claim cận cho input khác. |
| O-3 | Cao | **Chưa chứng minh/cài assertion cho các bất biến của Algorithm 1–3 và Lemma 3.3.** | `FindPivots` dùng active vector thay vì tập và lan root thay vì dựng forest cuối; `BaseCase` dùng lazy heap; `batchPQ` có các assert quan trọng bị comment. | Viết mapping/proof equivalence hoặc audit assertions: pre/postcondition recursion, unique/disjoint U/S, bounds, block ordering, Pull partition/bound, duplicate registry và quota. |
| O-4 | Trung bình | **Cận worst-case/amortized của implementation chưa được chứng nhận.** | Có cấu trúc code giống paper nhưng chưa audit toàn bộ chi phí selection, split, insert, batch prepend, Pull, iterator và biến thể số thực. | Xác minh chính thức/manual review từng invariant + amortization, test đối kháng; công bố rõ preconditions và mô hình tính chi phí. |
| O-5 | Trung bình | **Upstream và demo không phải cùng thuật toán byte-for-byte.** | Demo thêm trace hooks, clamp `k,t≥1` ở n=1 và bỏ rounding key `1e-10`; rounding upstream đã gây lỗi near-tie distance. | Giữ provenance/label rõ ba chế độ: upstream pristine, demo patched/instrumented, reference/audit; mọi benchmark ghi chính xác mode. |
| O-6 | Trung bình | **Benchmark visualizer chưa có timer scope hoàn toàn đồng nhất.** | BMSSP demo đo prepare+execute+BFS predecessor rebuild; Dijkstra đo dựng adjacency+heap và ghi predecessor khi relax; baseline upstream lại có scope khác. | Chuẩn hóa và công bố ít nhất core-only và end-to-end với phạm vi giống nhau; tách parse/setup/transform/path-output/trace/render. |
| O-7 | Theo dõi | **Rủi ro `batchPQ::it_min`/comparator chưa được tái hiện.** | Iterator mặc định được truyền vào `lower_bound`; comparator có thể dereference iterator khi primary key bằng nhau. Chưa xác nhận nhánh gây undefined behavior. | Reproduce hoặc chứng minh nhánh không xảy ra; thêm guard/test sanitizer/assertion trước khi khẳng định là lỗi. |

## 4. Bằng chứng thực nghiệm đã có

Những kết quả dưới đây chỉ chứng minh trên tập input hữu hạn; chúng **không phải** proof tổng quát hoặc proof cận độ phức tạp.

### Regression và test

- `python -m unittest discover -s C:\ttss\bmssp_visualizer\native -p 'test_*.py' -v`: **7/7 PASS**.
- `python -m unittest discover -s C:\ttss\bmssp_visualizer\tests -v`: **19/19 PASS**.
- Build CMake upstream C++, chạy trực tiếp `tests.exe`: **8/8 test cases, 345.272 assertions PASS**.
  - `ctest` không tìm thấy test vì `CMakeLists.txt` không đăng ký `add_test`; không phải executable test thất bại.

### Fuzz bổ sung trong session này

- 240 đồ thị ngẫu nhiên `n=2..300`, bốn họ: mixed, sparse, high-in-degree, high-out-degree; gồm trọng số 0, ties và near-ties. Mảng distance của runner BMSSP khớp Bellman–Ford độc lập với `rel_tol=abs_tol=1e-12`.
- 160 đồ thị ngẫu nhiên trọng số nguyên `0..7`; mảng distance khớp **chính xác** Bellman–Ford, gồm cả unreachable.

Các kết quả trên củng cố bằng chứng cho **distance array**; chúng không sửa O-1 và không đóng O-2 đến O-7.

## 5. Dijkstra và diễn giải hiệu năng

Dijkstra native tại `C:\ttss\bmssp_visualizer\native\dijkstra.cpp` là lazy binary min-heap đầy đủ:

- relax chỉ khi cải thiện nghiêm ngặt;
- bỏ stale heap entry;
- không dừng sớm tại target;
- không phải shortcut/fallback cho BMSSP.

Cận trực tiếp an toàn của implementation lazy này là `O(n + m log(m+1))`; với đồ thị đơn/thưa thường có thể viết `O((n+m) log n)`.

Dijkstra nhanh hơn BMSSP ở benchmark hiện có **không mâu thuẫn** với theorem của paper. `O(·)` là cận tiệm cận, bỏ hằng số ẩn và không áp đặt thời gian thực trên từng topology. BMSSP có recursion, FindPivots, batch queue, median selection và bookkeeping phức tạp; Dijkstra có hằng số nhỏ, locality tốt, và frontier có thể nhỏ.

Số liệu C++ cùng máy, đồ thị tổng hợp bậc hằng 50.000 đỉnh / 199.996 cạnh:

| Thuật toán | Trung vị |
|---|---:|
| Dijkstra C++ | 16,188 ms |
| BMSSP EXPECTED | 63,301 ms |
| BMSSP WC | 69,428 ms |

Nguồn: `C:\ttss\bmssp_comparison\REPORT.md`. Đây là benchmark giới hạn workload/máy đó, không phải xếp hạng phổ quát.

## 6. Khuyến nghị repository

- Giữ `lcs147/bmssp` làm nền C++ để trace/demo: có implementation Algorithm 1–3 và cả WC/expected variant.
- Dùng `primaryaesthetics/logtwothirds` tại `C:\ttss\bmssp_comparison\repos\rust_python` làm nguồn audit/spec/test: có `ALGORITHM.md`, `SPEC.md`, test cho transform/FindPivots/BlockDS và property tests.
- Chưa gọi repository nào là “formally verified” chỉ dựa vào README. Rust core chưa chạy được tại máy hiện tại vì chưa có Cargo; cần cài toolchain và chạy kiểm thử độc lập trước khi nâng mức tin cậy.
- Không dùng `bzantium` V2 làm solver tham chiếu: đã có phản ví dụ trả distance sai trong `C:\ttss\bmssp_comparison\audit.json`.

## 7. Điều kiện để chốt thành reference repo nghiêm túc

Theo thứ tự ưu tiên:

1. Sửa O-1: predecessor/path exactness và verifier exact-vs-epsilon tách biệt.
2. Sửa/hiển thị O-2: transformation bậc hằng và original/transformed mapping; ghi rõ miền áp dụng.
3. Thêm audit mode cho O-3/O-4: invariant của procedures và Lemma 3.3, cùng test adversarial/property.
4. Chuẩn hóa benchmark cho O-6: timer scope, seeds, sizes, topology, memory/operation counters.
5. Kiểm O-7 bằng sanitizer/reproduction/proof.
6. Cài Rust/Cargo, chạy độc lập `logtwothirds` mainline/reference test; phân biệt rõ mainline BMSSP với `bmssp-fast`/`auto`.

## 8. Quy tắc khi mở session tiếp theo

- Không nói các vấn đề O-1 đến O-7 đã giải quyết nếu chưa có code change, regression và bằng chứng mới.
- Không tăng mức tuyên bố từ “test pass/fuzz pass” thành “đúng tổng quát” hoặc “complexity-certified”.
- Giữ benchmark upstream, demo patched và reference/audit ở chế độ/provenance tách biệt.
- Nếu sửa solver hay runner, cập nhật `PROJECT_MEMORY.md`, tài liệu provenance và kết quả regression tương ứng.

---

**Chốt session:** Các vấn đề fidelity, predecessor exactness, điều kiện áp dụng complexity và fairness của benchmark **vẫn đang tồn tại, chưa được giải quyết trong session này**.