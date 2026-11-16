# BMSSP: demo, kiểm thử và khảo sát thực nghiệm

Dự án tái hiện BMSSP từ `2504.17033v2.pdf` và so sánh với Dijkstra C++ trên cùng đồ thị. Bản demo dùng bản sao đã sửa từ lcs147/bmssp; xem `bmssp_visualizer/native/README.md` để biết các thay đổi. Các kiểm thử không chứng minh đúng tổng quát hoặc chứng nhận cận worst-case của implementation.

## Chuẩn bị và kiểm tra

Windows/PowerShell, Python 3.11+, g++ hỗ trợ C++20 trên PATH. Python backend dùng thư viện chuẩn. Rust chỉ cần cho bộ đối chiếu Rust tùy chọn; phiên bản và kết quả thực tế được ghi trong manifest.

```powershell
python run_all.py --quick
```

Đọc PASS/FAIL/SKIPPED và file manifest được in ra. Kết quả upstream lỗi near-tie là diagnostic lịch sử, không đại diện cho runner demo đã sửa. Kiểm tra production không đòi hỏi bản upstream lỗi phải trả đúng.

## Mở demo

```powershell
python bmssp_visualizer/start_demo.py --dataset weighted_100 --no-autoplay
```

Chọn BMSSP hoặc Dijkstra, xem khoảng cách, cây và đường đi tới đích; xuất HTML/JSON/CSV để xem lại. Chế độ kiểm chứng phân biệt khớp chính xác theo binary64/miền số nguyên an toàn với đạt dung sai trên số thực dấu phẩy động. Thời gian animation hoặc một lần chạy trên UI không phải benchmark.

## Benchmark mới

```powershell
python bmssp_comparison/benchmark_v2.py --smoke
python bmssp_comparison/benchmark_v2.py
```

Smoke chạy 5 họ đồ thị ở n≈100; đầy đủ chạy 5 họ × 2 kích thước (n≈100/1000) × 3 seed, mỗi mode 2 warm-up và 7 mẫu. Giới hạn mỗi process 30 giây. Chạy tuần tự, xoay thứ tự mode. Ba mode: Dijkstra, patched BMSSP trực tiếp, cùng patched BMSSP có transform. Tập này dùng trọng số nguyên để đối chiếu chính xác khoảng cách và raw predecessor; ca số thực/near-tie nằm trong regression riêng.

Kết quả mới ở `bmssp_comparison/results_v2/<timestamp>/results.json` cùng input và fingerprint source. `setup_ms`, `solve_ms`, `parsed_to_result_ms` có phạm vi ghi ngay trong JSON; `n_actual/m_actual/max_in/max_out` từ đồ thị chuẩn bị thực của BMSSP. Profile chạy riêng nên không trộn chi phí đếm vào timing. Bộ đo mới tập trung C++; Rust trong báo cáo cũ giữ nhãn thực nghiệm và chưa chứng nhận cận lý thuyết. Các script `cross_check_and_bench.py`, `run_transform_bench.py` là bộ đo lịch sử, không dùng thay cho v2 để nghiệm thu phạm vi mới.

## Đọc kết luận

- `REMEDIATION_PLAN.md`: mốc chỉnh sửa và trạng thái.
- `bmssp_comparison/SIX_STAGES_REVIEW.md`: bằng chứng phát hiện trước sửa.
- `bmssp_visualizer/FIDELITY_REVIEW.md`: mapping code/PDF và những nghĩa vụ chứng minh còn mở.
- `bmssp_comparison/FIDELITY_AUDIT.md`: đối chiếu C++/Rust, giới hạn thực nghiệm.

BMSSP chậm hơn Dijkstra không tự chứng minh code sai. Cận tiệm cận tốt hơn không đảm bảo nhanh hơn trên mọi đồ thị hoặc quy mô đang thử. Mục tiêu repo là kết quả kiểm tra được, phép đo tái lập và công khai giới hạn.
