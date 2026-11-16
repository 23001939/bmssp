# Review độc lập kết quả 6 giai đoạn

Ngày 14/11/2026. Đọc code, PDF gốc và các tài liệu bàn giao hiện có; chạy lại 19 test app/API + 7 test native, tất cả đạt. Không chạy lại benchmark lớn hoặc toàn bộ Cargo tests, nên không xác nhận lại số 42/42 hay >3000 ca trong báo cáo. Kết luận: có nền tảng demo/test hoạt động, nhưng chưa đạt mức “mọi vấn đề đã đóng, chứng nhận đúng tuyệt đối và bảo toàn cận worst-case”.

## Các vấn đề phải sửa trước khi nghiệm thu

### P1 — O-1 được ghi đã sửa nhưng code vẫn chứa lỗi

FINAL_6_STAGES_REPORT.md và FRIEND_REQUEST_PLAN.md nói visualizer dùng trực tiếp pred khi relax. Thực tế native/runner.cpp:61–69 vẫn bỏ rawpred và dựng BFS với dung sai 1e-9. Phản ví dụ 0→1=4e-11, 0→2=0, 2→1=0 trả khoảng cách 0 nhưng predecessor của 1 là 0, tức chọn đường giá 4e-11 thay đường giá 0. Verifier vẫn ok=true. Bằng chứng lưu ở ../bmssp_visualizer/exports/fidelity-near-tie.json. Test hiện có chỉ xác nhận khoảng cách của ca này, không bắt đường đi không tối ưu chính xác. Không thể đóng O-1 ở visualizer.

### P1 — Chưa chứng nhận cận lý thuyết của bản Rust

repos/rust_python/src/block_queue.rs:98–125 dùng random-pivot quickselect; không phải phép chọn median tuyến tính worst-case tất định như định lý gốc. Dòng 297–298 chèn vào vector d1 và d1_bounds, dòng 465–466 remove(0) dịch phần tử; binary search để tìm vị trí không biến chi phí cập nhật vector thành O(log số khối). Cần phân tích riêng amortization và cấu trúc dữ liệu trước khi khẳng định cận. Test invariant không chứng minh complexity. Mapping trong FIDELITY_AUDIT.md gọi vector hoặc BST như tương đương về chi phí là không đủ. total_cmp cung cấp thứ tự so sánh float, không biến cộng f64 thành số thực chính xác hoặc bảo đảm đúng tuyệt đối mọi đầu vào.

C++ còn các khoảng trống FindPivots/sets/root propagation và tiền điều kiện bậc hằng, được mô tả trong ../bmssp_visualizer/FIDELITY_REVIEW.md. Vì vậy O-3/O-4 cần mở lại ở mức chứng nhận, dù bộ test hữu ích.

### P1 — Script 1-click có thể báo thành công dù chưa kiểm tra hoặc kiểm tra lỗi

run_all.py bỏ qua giá trị trả về check_toolchain(); run_checks chỉ in cảnh báo khi subprocess lỗi, không trả trạng thái thất bại; print_summary luôn in 100%/mọi vấn đề đã xử lý. Nếu không có script fidelity, nó tự in PASS từ tên tài liệu. Nếu transform_results.json tồn tại, nó in PASS mà không đọc kết quả. --limit truyền sang run_fidelity_checks.py không được script con parse; script con cố định total=100. Đây chưa phải cổng kiểm tra nghiệm thu đáng tin.

### P2 — Mô tả timer “chỉ execute” không khớp phép đo

bench_cpp.cpp:9–18 tạo samples_ms từ trước constructor và prepare; kernel_ms mới là khoảng chỉ execute. cross_check_and_bench.py:216–217 và run_transform_bench.py lấy samples_ms/median_ms thay kernel_ms. Rust examples/bench_rust.rs đặt timer trước sssp_bmssp; hàm này trong src/bmssp.rs:732 có transform. Không bao gồm đọc file không đồng nghĩa loại bỏ setup/transform. Số liệu vẫn có giá trị theo phạm vi thực tế, nhưng chưa chứng minh benchmark core-only như báo cáo; cũng chưa xác nhận mọi scope C++/Rust tương đương.

### P2 — Số đỉnh sau transform và nguyên nhân cache bị trình bày quá chắc chắn

run_transform_bench.py compute_expansion_metrics tính n'=2m, m'=3m từ đồ thị gốc; đây là công thức mô hình endpoint, không đọc kích thước thực từ các solver. C++ gộp láng giềng hai chiều bằng map và thêm đỉnh sentinel: với star hai chiều d lá, code cho 2d+1 đỉnh, không phải 4d như công thức script. Cần lấy thống kê trực tiếp mỗi implementation. Script không đo hardware cache counters; các phát biểu trượt L1/L2/L3 là giả thuyết giải thích, không phải nguyên nhân đã chứng minh bằng phép đo hiện có. So Rust bmssp với bmssp-fast cũng thay nhiều yếu tố, không cô lập riêng transform.

### P2 — Test gần đúng bị gọi exact/invariant proof

run_fidelity_checks.py so abs(diff)>1e-7 nhưng in “100% exact match”; kiểm tra cuối distances không đồng nghĩa kiểm tra bất biến nội bộ từng lời gọi. Các n/m/loại trọng số lấy global random không seed nên chưa tái lập hoàn toàn. FIDELITY_AUDIT.md gán “§7 What this is not” cho PDF gốc; mục đó cần quy về đúng tài liệu repo, không phải cấu trúc của PDF 2504.17033v2. Báo cáo cuối nói tác giả C++ từng công bố nhanh hơn 1.4–2.8 lần nhưng không đưa bằng chứng; cần dẫn nguồn đúng hoặc bỏ tuyên bố.

## Phần đã xác nhận

- 26 test app/native chạy lại đạt; API, live control và hai solver còn hoạt động ở phạm vi test.
- Có mã Rust benchmark, báo cáo, dữ liệu kết quả và các script bổ sung; đây là tiến bộ so với chỉ có demo.
- Lõi Dijkstra hiện tại là binary heap, chạy toàn SSSP; không phát hiện shortcut sai nhiệm vụ trong vòng lặp đã đọc.
- BMSSP chậm hơn Dijkstra không tự chỉ ra lỗi code hoặc mâu thuẫn Big-O. Cần tách kết luận hiệu năng, tính đúng kết quả, và chứng minh cận triển khai.

Ưu tiên: sửa đường đi + regression; sửa luồng PASS/FAIL của launcher; điều chỉnh báo cáo theo scope timer và số đo thật; mở lại các mục chứng minh fidelity/complexity chưa đủ bằng chứng. Review này không sửa solver hoặc ghi đè các báo cáo bàn giao.
