# Kế hoạch visualize và demo BMSSP

Cập nhật: 13/11/2026, Asia/Saigon.

**Tiến độ tổng: 100%.** Đã triển khai và kiểm chứng dữ liệu, giao diện, live runner, visualize kết quả, xuất file/mở lại và launcher cục bộ. Xem `bmssp_visualizer/README.md` và `bmssp_visualizer/VERIFICATION.md`.

## Kết quả người dùng cần

1. Xem đồ thị đầu vào, hiểu nguồn, cạnh và trọng số trước khi chạy.
2. Quan sát BMSSP thực thi: đỉnh/cạnh đang xử lý, pivot, relax và các lời gọi đệ quy.
3. Xem kết quả: khoảng cách, cây đường đi ngắn nhất và đường đi tới đích được chọn.
4. Có file đầu vào/đầu ra và bản visualize có thể mở lại, tải xuống và chia sẻ.
5. Nhận báo cáo % tổng tiến trình trong các lượt trao đổi và thường xuyên trong khi triển khai.

## Phương án đã triển khai

- Giao diện trình duyệt chạy cục bộ trên Windows, trong thư mục mới `bmssp_visualizer/`; áp dụng hướng dẫn Sites cho phần giao diện web.
- Python phục vụ API và truyền sự kiện từ runner C++ sang trình duyệt bằng SSE. Phần giao diện dùng HTML/CSS/JavaScript với SVG cho đồ thị nhỏ và Canvas cho đồ thị lớn.
- Dựa vào bản C++ WC `lcs147/bmssp`, theo kế hoạch đã được người dùng đồng ý triển khai. Bản sao demo có hook và bản vá số thực được ghi rõ trong native/README.md; baseline riêng dùng nguyên bản.
- Tạo bản runner có trace riêng, có provenance/khác biệt rõ ràng. Không thay runner benchmark nguyên bản bằng runner có instrumentation khi báo tốc độ.
- Wrapper hiện tại chỉ lấy distances, chưa xuất predecessor hoặc sự kiện bên trong thuật toán. Cần bổ sung hook có điều kiện trong bản nguồn dành riêng cho demo, gồm ánh xạ đỉnh gốc nếu có chuyển đổi bậc.
- Live demo có sự kiện thật từ runner đang chạy, không dựng hoạt ảnh giả từ khoảng cách cuối cùng.
- Chế độ chậm/từng bước dùng cơ chế điều khiển runner tại điểm phát sự kiện để người xem theo kịp; thời gian chờ được hiển thị riêng. Replay là chế độ phát lại trace đã ghi, phải ghi nhãn rõ.
- Đo tốc độ bằng lần chạy riêng không có trace, không cộng thời gian render, ghi log hoặc chờ thao tác người dùng vào benchmark.

## Luồng sử dụng

1. Chọn bộ dữ liệu có sẵn hoặc nhập file TXT/JSON; chọn đỉnh nguồn, có thể chọn đích để xem đường đi.
2. Xem đồ thị, số đỉnh/cạnh, loại trọng số; kéo, zoom, tìm đỉnh, xem trọng số cạnh.
3. Bấm Chạy; có Tạm dừng, Tiếp tục, Từng bước, điều chỉnh nhịp demo, Dừng và Đặt lại.
4. Quan sát cạnh đang relax, khoảng cách tạm thời, pivot, tập đỉnh của lần gọi và mức đệ quy. Chỉ đánh dấu complete khi điều kiện thuật toán bảo đảm, không dựa vào việc đỉnh vừa được phát hiện.
5. Chọn đích để tô đường đi và xem tổng chi phí; xem bảng distances/predecessors và trạng thái đối chiếu Dijkstra.
6. Xuất đồ thị đầu vào, kết quả, nhật ký và HTML tự chứa để mở lại.

## Dữ liệu và khả năng xem đồ thị lớn

- Thêm bộ minh họa 10–30 đỉnh có bố cục cố định để thấy đủ từng bước; vẫn sử dụng các bộ hiện có để kiểm tra lớn hơn.
- Với đồ thị nhỏ, hiển thị đầy đủ cạnh có hướng và trọng số; với hàng nghìn/hàng chục nghìn đỉnh, dùng tổng quan và vùng lân cận có giới hạn, ghi rõ đang xem phần nào.
- Không vẽ đầy đủ mọi nhãn hoặc phát hoạt ảnh từng cạnh cho đồ thị 50.000 đỉnh; cho phép lấy mẫu sự kiện khi chạy lớn và ghi rõ trace có bị lấy mẫu không.
- Vị trí đỉnh, seed, đỉnh nguồn và tập con đang xem cần được lưu để mở lại cùng bố cục.
- Đồ thị đường bộ New York là mở rộng sau: có dữ liệu DIMACS nhưng chưa có bộ tọa độ bản đồ được xác minh trong dự án; không hứa hiển thị bản đồ địa lý từ file cạnh đơn thuần.

## File đầu vào/đầu ra dự kiến

| File | Nội dung | Cách visualize |
|---|---|---|
| `graph.json` | n, edges có ID, source, tùy chọn target, positions và metadata | Nạp lại đồ thị đầu vào với cùng bố cục |
| `result.json` | Khoảng cách, predecessor/ID cạnh cha, source, cấu hình solver, thời gian, kiểm chứng và metadata | Cây đường đi ngắn nhất, tô đường đi, bảng khoảng cách |
| `trace.ndjson` | Sự kiện theo dòng: run ID, sequence, thời điểm tương đối, mức gọi, loại sự kiện và thay đổi liên quan | Phát lại diễn biến có thứ tự; ghi rõ full/sampled |
| `demo.html` | Giao diện tự chứa, dữ liệu, bố cục, kết quả và trace phù hợp kích thước | Mở trực tiếp trên trình duyệt, không cần server cho việc xem lại |
| `distances.csv` | Vertex, distance, predecessor và trạng thái reachable | Mở Excel hoặc dùng công cụ phân tích khác |
| `input.svg` / `output.svg` | Ảnh vector của vùng đồ thị đang xem, có chú giải | Chèn vào báo cáo và phóng lớn |

JSON dùng `null` và trạng thái reachable cho đỉnh không tới được; không xuất Infinity/NaN thành JSON không chuẩn. Cạnh song song cần ID để khôi phục và kiểm tra chính xác chi phí đường đi. Với trace lớn, export tự chứa phải có giới hạn rõ và hỗ trợ chọn vùng/trace rút gọn thay vì làm treo trình duyệt.

## Mốc công việc và cách tính % tổng

| Mốc | Trọng số | Tổng khi hoàn thành | Trạng thái | Điều kiện hoàn thành |
|---|---:|---:|---|---|
| 1. Rà soát và lập kế hoạch | 10% | 10% | Đã xong | Phạm vi, file xuất, cách live và cách đo rõ ràng; lưu quy tắc tiến độ |
| 2. Chuẩn hóa dữ liệu và kết quả | 15% | 25% | Đã xong | Bộ demo nhỏ; parser TXT/JSON; source tùy chọn; schema graph/result/trace; predecessor và unreachable |
| 3. Visualize đầu vào | 20% | 45% | Đã xong | Xem đồ thị, zoom/pan, chọn nguồn/đích, xem cạnh; giới hạn phù hợp đồ thị lớn |
| 4. Runner và demo trực tiếp | 25% | 70% | Đã xong | Sự kiện thật; stream; chạy/dừng/tạm dừng/từng bước; hiển thị pivot/relax/đệ quy; đo riêng overhead |
| 5. Visualize đầu ra và xuất file | 20% | 90% | Đã xong | Cây đường đi, đường đi đến đích, bảng khoảng cách, tải graph/result/trace/CSV/SVG/HTML mở lại |
| 6. Kiểm thử và hướng dẫn | 10% | 100% | Đã xong | Kiểm chứng tính đúng, điều khiển demo, export/reopen và đồ thị lớn; lệnh chạy và handoff cụ thể |

% có thể tăng trong một mốc khi có đầu việc hoàn tất và bằng chứng rõ ràng. Mốc cuối chỉ đạt 100% khi trải nghiệm bắt buộc đã được kiểm chứng; nếu thiếu môi trường kiểm tra giao diện, phải nêu giới hạn thay vì tự đánh dấu đã xác minh.

## Kiểm chứng cần có

- Đồ thị nhỏ có đáp án biết trước; trọng số 0; hòa khoảng cách; chu trình; cạnh song song; đỉnh không tới được; nguồn khác 0.
- So distances của runner trace với Dijkstra và runner nguyên bản; kiểm tra predecessor tạo đường đi hợp lệ, không có chu trình và đúng tổng trọng số.
- Không gắn mọi đỉnh vào một thứ tự Dijkstra giả: BMSSP có thể hoàn tất theo lô và thứ tự khác nhau.
- Live events có sequence liên tục hoặc thông tin lấy mẫu, bắt đầu/kết thúc rõ; không trộn event của hai lần chạy khi reset/dừng.
- Tạm dừng/từng bước có tác động thực với runner ở chế độ điều khiển; replay được ghi nhãn riêng.
- Mở lại `demo.html` và nạp `graph.json`/`result.json` để kiểm tra khả năng xem lại; đường đi được chọn khớp dữ liệu.
- Kiểm tra đồ thị lớn bằng tổng quan/vùng chọn, không lấy trải nghiệm 20 đỉnh để kết luận đã hỗ trợ 50.000 đỉnh.
- Kiểm tra hiệu năng bằng runner nguyên bản theo phương pháp benchmark đã có. Không tự so thời gian có animation với số liệu cũ.

## Quy tắc báo cáo tiến độ

Mỗi lượt trả lời về dự án có dòng **Tiến độ tổng: X% — nội dung cụ thể**. Khi làm dài, cập nhật tối đa khoảng 60 giây giữa các thông báo; luôn báo khi hoàn thành mốc hoặc gặp thay đổi quan trọng. Nêu đã xong gì và bước kế tiếp. Lưu cùng trạng thái vào file này và `PROJECT_MEMORY.md`. Nếu chỉ trả lời câu hỏi hoặc chờ thông tin, giữ nguyên % trừ khi có hạng mục thực sự hoàn thành.

## Sửa lỗi đổi dữ liệu sau bàn giao

Tiến độ sửa lỗi: 100%. Đã tái hiện lỗi SVG cũ vẫn hiện khi dataset mới dùng Canvas; thay `.hidden` bằng toggleAttribute, xóa SVG cũ và reset view/timeline. Đã kiểm tra hình thực sự hiển thị (computed style, vị trí trong khung, pixel Canvas), đổi dataset nhỏ/lớn/nhỏ và nhập TXT/JSON. Đã tạo lại HTML/ảnh/ZIP để loại bản xuất cũ có lỗi. Mở lại trang hoặc Ctrl+F5 để nạp JavaScript mới.
