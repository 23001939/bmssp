# BMSSP Visualizer cục bộ

Công cụ thử nghiệm để xem đồ thị đầu vào, theo dõi runner BMSSP có phát sự kiện và kiểm tra kết quả bằng Dijkstra độc lập. Chạy tại máy; không cần cài thư viện Python ngoài.

Sau chỉnh sửa 14/11/2026: runner kiểm tra predecessor thật, fallback chỉ dùng cạnh khớp binary64 chính xác; UI/JSON phân biệt exact và tolerance cho khoảng cách lẫn đường đi. Xem `../REMEDIATION_RESULTS.md` và README ở gốc cho lệnh nghiệm thu mới. Bộ benchmark chuẩn hóa mới là `../bmssp_comparison/benchmark_v2.py`; thời gian demo/baseline cũ giữ phạm vi riêng. Tính đúng tổng quát và cận của toàn implementation chưa được chứng nhận.

## Chạy

Mở PowerShell tại thư mục gốc repo và chạy:

```powershell
python bmssp_visualizer/server.py --port 8765
```

Mở `http://127.0.0.1:8765`. Lần đầu, backend biên dịch runner C++ nếu chưa có binary. Cần Python 3 và trình biên dịch C++20 khả dụng trên `PATH` (ví dụ `g++`). Dừng máy chủ bằng `Ctrl+C`.

Cách mở tự động (khởi động server nền nếu cần và mở trình duyệt):

```powershell
python bmssp_visualizer/start_demo.py
```

Nếu muốn mở giao diện trước rồi tự bấm Chạy, thêm `--no-autoplay`. Server nền ghi log trong `bmssp_visualizer/server.log`.

Mở `http://127.0.0.1:8765/?demo=live` để bắt đầu ngay demo trực tiếp 18 đỉnh, nhịp 80 ms/sự kiện. Có thể Tạm dừng và Từng bước để quan sát. Mỗi lần mở URL này tạo một lần chạy thật; hãy dừng lần chạy cũ trước nếu còn hoạt động.

Để nạp đồ thị có trọng số 100 đỉnh, mở `http://127.0.0.1:8765/?dataset=weighted_100` hoặc chạy `python bmssp_visualizer/start_demo.py --dataset weighted_100 --no-autoplay`. Đồ thị thưa này dùng seed `20261114`, có 100 đỉnh và 240 cạnh trọng số nguyên 1–20, nguồn 0, đích 99 và được tạo để mọi đỉnh đều tới được. Bấm **Chạy BMSSP** hoặc **Chạy Dijkstra**; hai thuật toán dùng cùng đồ thị và nguồn. Bảng bên dưới giữ kết quả của cả hai, xóa khi đổi đồ thị hoặc nguồn. Đường từ 0 đến 99 có chi phí 93. Dijkstra là solver C++ binary heap riêng, đối chiếu bằng Python độc lập. Một lần chạy demo hoặc so kết quả không phải benchmark hiệu năng.

Đầu vào đã lưu tại `bmssp_visualizer/data/weighted_100.json` và `.txt`; kết quả, hai HTML ngoại tuyến và SVG ở `bmssp_visualizer/exports/weighted_100/`. `comparison-fast.json` giữ hai lần đo không trace và kết quả đầy đủ; hash đầu vào và nguồn cho biết cả hai lần dùng cùng đồ thị.

Các file minh họa đã xuất nằm ở `bmssp_visualizer/exports/demo/`: `demo.html` (mở trực tiếp không cần server), `graph.json`, `result.json`, `trace.ndjson`, `distances.csv`, `input.svg`, `output.svg` và ảnh chụp phiên thật. Trong HTML ngoại tuyến, chọn Phát lại trace để xem diễn biến đã ghi. Nạp `graph.json` rồi `result.json` nếu muốn xem lại hai file dữ liệu rời.

## Dữ liệu

Có thể chọn bộ dữ liệu có sẵn trong giao diện hoặc nạp TXT/JSON. TXT dùng dòng đầu `n m`, sau đó đúng `m` dòng `u v w`; đỉnh đánh số từ 0, cạnh có hướng, trọng số hữu hạn và không âm. JSON nhận dạng `{ "n": ..., "edges": [[u,v,w], ...] }` hoặc schema graph đã chuẩn hóa. Cạnh song song và trọng số 0 được hỗ trợ. Đỉnh không tới được được biểu diễn bằng `null` khi xuất JSON.

## Chạy và xem lại

Chế độ trực tiếp gửi sự kiện từ thuật toán đang chạy. Chế độ điều khiển có thể đợi thao tác tiếp tục/từng bước cho cả BMSSP và Dijkstra; thời gian chờ và vẽ sự kiện không phải thời gian benchmark. Chế độ nhanh chạy riêng không có nhịp animation để lấy kết quả và thời gian tính toán; với BMSSP, backend cũng báo riêng một lần đo runner upstream nguyên bản khi khả dụng. Đây là một lần chạy đơn lẻ, không phải trung vị benchmark. Kết quả được kiểm tra bằng Dijkstra phía Python; kiểm chứng trên một đồ thị không phải chứng minh tổng quát cho thuật toán.

Trace có thể tải về để phát lại. Phát lại là xem các sự kiện đã ghi, không phải lần chạy thuật toán mới. Có thể xuất đồ thị, kết quả, trace, CSV hoặc HTML tự chứa để mở ngoại tuyến (tùy các định dạng được giao diện cung cấp).

## Kích thước lớn

Đồ thị lớn, gồm bộ 50.000 đỉnh, dùng tổng quan và vùng lân cận giới hạn để giao diện không cố vẽ toàn bộ nhãn/cạnh cùng lúc. Số lượng phần tử hiển thị có thể là một phần của đồ thị; hãy xem thông tin giới hạn mẫu/vùng trên giao diện và trace trước khi diễn giải. Giới hạn phần xem không làm thay đổi dữ liệu đầu vào mà runner xử lý.

## Kiểm thử

Sau khi runner có thể biên dịch, chạy bộ kiểm thử thư viện chuẩn:

```powershell
python -m unittest discover -s bmssp_visualizer/tests -v
```

Các kiểm thử bao gồm kiểm tra dữ liệu, so sánh runner với Dijkstra, trace có ACK và endpoint/export của máy chủ. Chỉ coi những kiểm thử thực sự chạy thành công trong môi trường của bạn là đã xác nhận.

## Nguồn gốc và giới hạn

Runner demo là bản instrumented riêng dựa trên triển khai C++ WC của [`lcs147/bmssp`](https://github.com/lcs147/bmssp); trace bổ sung sự kiện và cơ chế chờ điều khiển, nên không dùng số thời gian của nó để thay benchmark upstream. Repo upstream được giữ nguyên; phần chỉnh sửa cho demo nằm trong `bmssp_visualizer/native/`. Chi tiết provenance và license nằm cạnh mã runner.

Bản demo còn sửa phép so sánh khóa khoảng cách: upstream làm tròn tới `1e-10`, có thể bỏ sót cải thiện rất nhỏ; bản demo giữ nguyên giá trị số thực của máy. Ví dụ `0→1=4e-11`, `0→2=0`, `2→1=0`: upstream có thể trả `4e-11` cho đỉnh 1, trong khi đáp án là 0. Lần đo `baseline_ms` vẫn dùng mã upstream nguyên bản, tách khỏi bản demo đã sửa. Dung sai đối chiếu là `1e-9`; JSON cũng ghi số đỉnh không khớp chính xác và sai số tuyệt đối lớn nhất. Cây predecessor của runner được dựng từ các cạnh phù hợp khoảng cách, rồi kiểm tra chu trình và tổng chi phí từng đường đi.

Các lần chạy ghi vào `bmssp_visualizer/runs/<run_id>/`. Backend giữ danh mục phiên trong bộ nhớ; sau khi khởi động lại server, dùng các file đã xuất để xem lại phiên cũ. Với đồ thị lớn, trace có lấy mẫu và giới hạn số sự kiện được ghi; kết quả cuối vẫn gồm khoảng cách của toàn bộ đồ thị.

Kiểm tra hồi quy cho bản vá số thực:

```powershell
python -m unittest discover -s bmssp_visualizer/native -p 'test_*.py' -v
```

Đã kiểm tra Chrome trực tiếp và HTML ngoại tuyến; bằng chứng được lưu trong `verification_browser.json`, `verification_large.json`. Script kiểm tra trình duyệt sử dụng Chrome đã cài và gói `websocket-client` có sẵn trong môi trường phát triển; ứng dụng và file HTML xuất không cần gói này.

Đây là bản trực quan hóa phục vụ nghiên cứu, không đại diện cho mọi triển khai BMSSP hoặc đảm bảo hiệu năng tốt hơn Dijkstra. Kết quả đúng trên các bộ ca kiểm thử hữu hạn không chứng minh đúng trên mọi đồ thị.
