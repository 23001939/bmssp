# Bằng chứng kiểm chứng

Bổ sung 14/11/2026: đồ thị weighted_100 có100 đỉnh/240 cạnh và hai nút chạy Dijkstra/BMSSP thật. 19 unittest app/API và7 native regression đạt. Browser_compare.py kiểm tra số đỉnhSVG thực tế, fast vàcontrolled, pause/step cho cảhai, graph_hash+source+100 khoảng cách giống nhau, chi phí0→99=93, reset bảng khi đổi nguồn, offline HTML vàreplay cho cảhai, khôngJSexception. Bằng chứng verification_compare.json, exports/weighted_100/ và weighted-100-demo.zip.

Đã kiểm tra tại máy ngày 13/11/2026. Phạm vi visualize đã hoàn thành 100% theo VISUALIZATION_PLAN.md.

- 15 unittest tại tests/: parser, dữ liệu không hợp lệ, nguồn khác 0, trọng số 0/hòa, cạnh song song, đỉnh không tới được, chu trình predecessor, native/Dijkstra, ACK, HTTP, control, export và tách run ID.
- 4 regression tại native/: phản ví dụ near-tie của upstream, nguồn khác 0/parallel/zero/unreachable, lấy mẫu và một đỉnh.
- Chrome thực qua CDP: đầu vào 18 đỉnh; live SSE; pause giữ nguyên số sự kiện; step cho đúng một native event rồi dừng; resume đến kết quả; đường tới đỉnh 16 chi phí 11; đỉnh 17 không tới được.
- HTML xuất từ server và từ trình duyệt mở bằng file://, nhận dữ liệu và replay đến kết quả mà không cần API; nhập lại graph.json, result.json và trace.ndjson qua file input.
- Mobile 390×844: không tràn ngang trang. Đây không thay thế kiểm thử mọi thiết bị.
- Dữ liệu 50.000 đỉnh/199.996 cạnh: Canvas hiển thị vùng 200 đỉnh có nhãn giới hạn, tìm đỉnh 43210, tính toàn bộ kết quả và đối chiếu Dijkstra đạt, có lần đo upstream riêng.
- Không ghi nhận JavaScript exception trong các lượt kiểm tra trên.

Sửa lỗi sau phản hồi người dùng: kiểm tra cũ dùng property `.hidden` nên bỏ sót SVG vẫn hiện trên Canvas. Regression mới `browser_switch.py` tái hiện thất bại trước bản sửa và đạt sau bản sửa, xác minh computed display và Canvas thực sự trong khung/có pixel. Đã kiểm tra đổi demo→real_10000→demo, nhập TXT 5 đỉnh, nhập JSON 2.000 đỉnh. Các file HTML và ảnh kiểm chứng lớn được tạo lại; bằng chứng ở verification_switch.json. Việc nạp lại trang là cần thiết cho tab đang dùng JavaScript cũ.

Kết quả có cấu trúc: verification_browser.json, verification_import.json, verification_large.json. Ảnh thực và các file mở lại nằm trong exports/demo/. Bộ ZIP bàn giao ở exports/bmssp-demo-results.zip.

Mã nguồn upstream không bị sửa. Bản sao native có sửa khóa so sánh số thực và tham số một đỉnh, thêm trace/ACK; ghi rõ trong native/README.md. Tính đúng ở đây được kiểm tra trên các ca hữu hạn và theo dung sai 1e-9, không phải chứng minh tổng quát. Metadata chứa sai lệch chính xác và phương pháp dựng predecessor. Thời gian hoạt ảnh và phép đo riêng không trộn với benchmark lịch sử.

Các lệnh kiểm tra:

```powershell
python -m unittest discover -s bmssp_visualizer/tests -v
python -m unittest discover -s bmssp_visualizer/native -p 'test_*.py' -v
node --check bmssp_visualizer/dist/app.js
```

Các script browser_flow.py, browser_large.py và browser_import.py dùng Chrome cài sẵn cùng websocket-client trong môi trường phát triển. Không cần những công cụ này để chạy ứng dụng hoặc mở HTML đã xuất.
