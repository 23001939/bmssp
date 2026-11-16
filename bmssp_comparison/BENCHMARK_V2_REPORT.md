# Benchmark sau chỉnh sửa

Kết quả chính: `results_v2/20261114-092651/results.json`. Các lượt smoke và chạy thử trước đó không dùng làm kết quả nghiệm thu. Chạy lại bằng `python bmssp_comparison/benchmark_v2.py`; mỗi lượt ghi thư mục mới.

30 cấu hình: 5 họ × 2 kích thước × 3 seed. Mỗi cấu hình đo Dijkstra, patched C++ BMSSP trực tiếp và cùng BMSSP bật transform; 2 warm-up + 7 mẫu mỗi mode, xoay thứ tự, chạy tuần tự. Tổng 810 lượt gồm warm-up, 630 mẫu đo và 90 lượt profile riêng. Tất cả kiểm tra khoảng cách và raw predecessor chính xác theo số học binary64 trên dữ liệu nguyên đều đạt. Kiểm tra transform yêu cầu bậc vào/ra không vượt 2. Không xem đây là chứng minh đúng tổng quát.

Thời gian dưới đây (ms) là trung vị của ba trung vị theo seed ở kích thước lớn, **từ input đã parse tới mảng khoảng cách và predecessor**, gồm copy/prepare và execute, loại I/O, JSON, hủy solver và verifier. Các pha setup và solve riêng nằm trong JSON. So sánh solve-only chạy trên biểu diễn đã chuẩn bị khác nhau phải xét cả n'/m'; không bỏ chi phí transform khi đánh giá tổng nhiệm vụ.

| Họ | n | Dijkstra | BMSSP trực tiếp | BMSSP transform |
|---|---:|---:|---:|---:|
| Chain có shortcut | 1000 | 0.080 | 0.857 | 1.953 |
| Grid hai chiều | 961 | 0.147 | 0.853 | 4.035 |
| Sparse | 1000 | 0.174 | 0.905 | 10.467 |
| Star có cạnh phụ | 1000 | 0.171 | 1.877 | 4.352 |
| Dense vừa, bậc ≈√n | 1000 | 0.553 | 1.289 | 80.595 |

Ví dụ seed20261114, kích thước thực sau transform: chain n'=2198,m'=3297; grid n'=3720,m'=7440; sparse n'=7946,m'=11927; star n'=3998,m'=6996; dense n'=62000,m'=93000. Các số này lấy từ `graph_stats()` sau prepare, không từ công thức 2m/3m; original-to-prepared mapping trong profile. Cạnh song song được gộp và self-loop không âm bỏ ở nhánh transform nên kích thước có thể khác mô hình endpoint.

Profile riêng chứa số relax thành công, heap pops/peak của Dijkstra, lời gọi recursion/pivot và số Insert/Pull/BatchPrepend/prepend-items của BMSSP. Đó là số thao tác, không phải phân bổ thời gian mỗi pha hoặc đo cache miss. Các counter tắt trong lượt timing; kiểm tra cờ vẫn là một phần của bản cài đặt đang đo.

Hạn chế: dữ liệu tổng hợp, kích thước tối đa gốc1000; không đo lại Rust trong bộ mới, không suy ngưỡng vượt từ kết quả này. Ghi compiler/flags/platform, fingerprint nguồn và input, mọi mẫu và lỗi trong JSON. Những lần chạy trên cùng máy còn phụ thuộc tải nền của hệ điều hành. Chỉ kết luận Dijkstra nhanh hơn trên các cấu hình đã thử, không suy rằng BMSSP sai hoặc luôn chậm hơn.
