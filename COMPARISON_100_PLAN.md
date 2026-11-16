# Đồ thị 100 đỉnh và hai thuật toán

Cập nhật 14/11/2026, Asia/Saigon. Đây là phần mở rộng mới; phạm vi visualize trước đó đã hoàn thành và được giữ nguyên.

Tiến độ tổng phần mở rộng: 100%.

| Mốc | Trọng số | Trạng thái |
|---|---:|---|
| Đồ thị có hướng 100 đỉnh, trọng số dương, dữ liệu và bố cục cố định | 20% | Đã lưu TXT/JSON |
| Dijkstra C++ thực và backend chọn thuật toán, cùng dữ liệu/nguồn | 30% | Đã xong; API kiểm chứng đạt |
| Hai nút chạy và bảng kết quả theo cùng đồ thị/nguồn | 25% | Đã xong |
| Kiểm chứng hai kết quả, Chrome, xuất file và bàn giao | 25% | Đã xong; kết quả và hash đầu vào khớp, live/replay đạt |

Đồ thị `weighted_100`: 100 đỉnh, 240 cạnh có hướng, trọng số nguyên 1–20, seed 20261114. Bố cục lưới 10×10 có cạnh ngược và đường chéo. Nguồn mặc định 0, đích 99; mọi đỉnh tới được từ 0. Dữ liệu này được dùng nguyên vẹn cho cả hai thuật toán.

Chế độ trực tiếp có sự kiện từ runner thật và ACK; chế độ nhanh không chờ hoạt ảnh. Thời gian một lần chạy ở quy mô 100 đỉnh chỉ minh họa các bản triển khai, không dùng để chứng minh giới hạn tiệm cận hoặc khẳng định thuật toán nào tốt hơn tổng quát. So thời gian fast riêng, không dùng wall time có pause/delay.

Phân công được người dùng cho phép: Terra phụ trách native Dijkstra; Sol phụ trách giao diện; Luna phụ trách QA/tài liệu; root tạo dữ liệu, nối backend, kiểm tra Chrome và bàn giao.

Bàn giao: `http://127.0.0.1:8765/?dataset=weighted_100`. Hai nút Chạy BMSSP / Chạy Dijkstra dùng đúng cùng dữ liệu và nguồn. Đường đi 0→99 có chi phí93 cho cả hai; toàn bộ100 khoảng cách khớp. Dữ liệu ở `bmssp_visualizer/data/weighted_100.json` và `.txt`; recordings/kết quả ở `bmssp_visualizer/exports/weighted_100/`, ZIP `bmssp_visualizer/exports/weighted-100-demo.zip`.

Kiểm chứng: 19 unittest dữ liệu/native/API và7 native regression đạt. Chrome kiểm tra100 đỉnh thực sự hiển thị, hai nút chạy fast vàcontrolled, pause/step/resume, bằng nhau của graph_hash+source+distances, reset bảng khi đổi source, HTML ngoại tuyến và replay cho cảhai; không cóJSexception. Bằng chứng `bmssp_visualizer/verification_compare.json` và ảnh trong exports/weighted_100/.
