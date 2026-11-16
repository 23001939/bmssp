# Hướng dẫn làm việc với dự án BMSSP

## Bối cảnh

- Thư mục dự án: `C:\ttss`; môi trường hiện tại là Windows / PowerShell.
- Trao đổi với người dùng bằng tiếng Việt. Giải thích kết luận, số liệu và giới hạn rõ ràng.
- Dự án đang nghiên cứu bài báo BMSSP và khảo sát các repo triển khai để chọn nền tảng phù hợp. Người dùng chưa chốt ngôn ngữ hay repo để phát triển tiếp.
- Đọc `PROJECT_MEMORY.md` khi bắt đầu công việc liên quan đến dự án. Xem các báo cáo và kết quả thô được dẫn ở đó trước khi lặp lại nghiên cứu hoặc benchmark.
- Phạm vi mới: visualize đầu vào, demo BMSSP trực tiếp và xuất kết quả có thể visualize/xem lại. Đọc `VISUALIZATION_PLAN.md` để biết kế hoạch và mốc tiến độ.
- Người dùng đã đồng ý triển khai kế hoạch cục bộ và cho phép dùng subagent khi cần, chọn model theo độ khó. Có thể giao phần thuật toán nặng cho model chuyên code (Terra), giao diện cho Sol, kiểm tra/tài liệu có phạm vi rõ cho Luna; root tích hợp và xác minh. Không cần phân công nhiều agent cho tác vụ đơn giản.

## Quy tắc báo cáo tiến độ do người dùng yêu cầu

- Trong mỗi lượt trả lời về dự án, báo **Tiến độ tổng: X% — việc đang làm/đã xong**. Với lượt giải thích ngắn, có thể giữ nguyên % nếu chưa hoàn thành thêm đầu việc.
- Khi đang làm việc nhiều bước, cập nhật tiến độ thường xuyên, tối đa khoảng 60 giây giữa các thông báo; cập nhật thêm khi hoàn thành mốc, phát hiện lỗi hoặc đổi hướng đáng kể.
- % là tiến độ của toàn bộ phạm vi visualize + live demo + xuất kết quả + kiểm chứng trong `VISUALIZATION_PLAN.md`, không phải riêng lượt trả lời hay riêng việc lập kế hoạch.
- Dùng trọng số các mốc làm căn cứ; nêu rõ phần việc còn lại. Không tăng % chỉ vì thời gian trôi qua và không báo 100% khi còn hạng mục bắt buộc chưa xong.
- Khi người dùng đổi phạm vi, giải thích việc điều chỉnh mốc/trọng số rồi cập nhật kế hoạch và bộ nhớ. Không âm thầm đặt lại tiến độ.
- Lưu trạng thái mốc trong `VISUALIZATION_PLAN.md` và phần tóm tắt trong `PROJECT_MEMORY.md` để phiên sau tiếp tục cùng thang đo.

## Tài liệu và cấu trúc

- `2504.17033v2.pdf`: bài báo gốc, nguồn đối chiếu thuật toán.
- `FRIEND_REQUEST_PLAN.md`: kế hoạch 6 giai đoạn đáp ứng yêu cầu bạn người dùng (source code đúng + benchmark), mốc tiến độ và tiêu chí hoàn thành. Phiên sau bắt đầu từ đây.
- `VISUALIZATION_PLAN.md`: kế hoạch visualize, demo trực tiếp, định dạng xuất và tiến độ tổng.
- `COMPARISON_100_PLAN.md`: phần mở rộng 100 đỉnh và hai thuật toán; đã hoàn thành. Dữ liệu weighted_100 và các bản HTML xuất ở bmssp_visualizer/data/ và exports/weighted_100/.
- `MIZCHI_DEMO_PLAN.md`: kế hoạch xây demo visualizer cho repo Rust `mizchi/check-arxiv-2504-17033` (DMM+25, C-HD, Dijkstra). 6 mốc, tiến độ riêng, port 8766. Phiên sau bắt đầu từ M1.
- `bmssp_visualizer/README.md`: cách chạy ứng dụng visualize, demo live, mở HTML ngoại tuyến và kiểm tra.
- `bmssp_visualizer/CONTRACT.md`: giao thức graph/result/trace và API giữa C++, Python và trình duyệt.
- `bmssp_comparison/repos/`: sáu repo GitHub đã tải; mỗi repo có lịch sử Git riêng.
- `bmssp_comparison/REPORT.md`: báo cáo benchmark tại máy và các giới hạn.
- `bmssp_comparison/results.json`: số liệu cuối cùng, môi trường và commit từng repo.
- `bmssp_comparison/audit.json`: phản ví dụ và chẩn đoán bài toán cơ sở.
- `bmssp_comparison/data/`: dữ liệu dùng chung và khoảng cách xuất từ C++.
- `bmssp_comparison/bench_cpp.cpp`, `bench_python.py`: wrapper đo các bản triển khai.
- `bmssp_comparison/run_comparison.py`: sinh dữ liệu, kiểm tra kết quả và chạy benchmark.
- `bmssp_comparison/audit_variants.py`: tái lập lỗi và kiểm tra workload của base case.
- `bmssp_comparison/write_report.py`: sinh báo cáo từ kết quả cuối cùng.
- `smoke_results.json` chỉ là kết quả kiểm tra sơ bộ; dùng `results.json` để báo cáo kết quả cuối cùng.

## Nguyên tắc nghiên cứu và sửa mã

- Phân biệt thuật toán gốc, bản triển khai giản lược, biến thể tối ưu và Dijkstra fallback.
- Khi cần bám sát bài báo, đối chiếu `FindPivots`, `BaseCase`, `BMSSP`, cấu trúc dữ liệu Lemma 3.3 và các điều kiện đầu vào/đầu ra.
- Chú ý relax với `<=`, giới hạn khoảng cách nửa mở, xử lý trọng số 0 và khoảng cách bằng nhau, cùng cutoff `k+1` của base case.
- Kiểm tra đúng trước khi xếp hạng tốc độ. Đối chiếu toàn bộ khoảng cách bằng một solver độc lập; ghi lại sai số và các ca thất bại.
- Khi kiểm tra chuyển SVG/Canvas, xác minh `getComputedStyle(...).display`, vị trí trong viewport và nội dung được vẽ. Không chỉ kiểm tra `.hidden`: thuộc tính này trên SVG có thể không phản chiếu thành attribute, để hình cũ vẫn hiện.
- Chỉ so trực tiếp thời gian trên cùng máy, cùng đồ thị, cùng nguồn và cùng phạm vi công việc. Không gộp benchmark từ README của các repo vào bảng đo tại máy.
- Nêu rõ có tính khởi tạo, chuyển đổi bậc đồ thị, dựng dữ liệu, đọc file và khôi phục đường đi hay không.
- Chạy các phép đo tốc độ tuần tự để hạn chế tranh chấp CPU; giữ seed, mẫu đo và commit để tái lập.
- Kết quả đúng trên các ca đã thử không phải chứng minh đúng tổng quát. Bản Python tham chiếu không đại diện cho hiệu năng lõi Rust.
- Hiện tại các repo upstream giữ nguyên; thay đổi phục vụ so sánh nằm trong wrapper bên ngoài. Nếu chỉnh upstream, ghi rõ thay đổi và không tiếp tục mô tả bản đã sửa là bản gốc.
- Runner visualize có bản sao riêng và bản vá bỏ làm tròn khóa `1e-10`; baseline riêng dùng upstream gốc. Giữ provenance và các chế độ này tách biệt; xem native/README.md.
- Không suy rộng kết luận "nhanh nhất trong các bản đã đo" thành "nhanh nhất cả sáu repo".

## Quy ước phong cách phản hồi (Claude Style)

- Trình bày mạch lạc, cấu trúc rõ ràng, sử dụng bảng biểu và danh sách có trật tự để làm nổi bật dữ liệu kỹ thuật.
- Điềm tĩnh, khách quan, phân tích sâu; tránh thái độ phô trương hoặc dùng biểu tượng cảm xúc thừa thãi.
- Giữ vững tính trung thực học thuật: luôn phân định ranh giới giữa định lý tiệm cận lý thuyết và kết quả đo đạc thực nghiệm trên phần cứng. Không tuyên bố đúng tổng quát chỉ dựa trên fuzz/test pass hữu hạn.
- Minh bạch phương pháp: nêu rõ ràng mã nguồn gốc (provenance), commit, compiler flags, môi trường thử nghiệm và các biến số phụ thuộc.


## Lệnh tái lập

Chạy từ thư mục gốc bằng PowerShell:

```powershell
g++ -std=c++20 -O3 -DNDEBUG bmssp_comparison/bench_cpp.cpp -o bmssp_comparison/bench_cpp.exe
python -X utf8 bmssp_comparison/run_comparison.py
python -X utf8 bmssp_comparison/audit_variants.py
python -X utf8 bmssp_comparison/write_report.py
```

Các lệnh này tạo lại dữ liệu và ghi đè các kết quả tương ứng. Không cần chạy lại benchmark chỉ vì sửa tài liệu.

## Duy trì bộ nhớ

- Cập nhật `PROJECT_MEMORY.md` khi có quyết định của người dùng, phát hiện mới hoặc kết quả thay đổi.
- Ghi ngày, nguồn bằng chứng và giới hạn của kết luận. Phân biệt đề xuất của trợ lý với quyết định đã được người dùng chấp nhận.
- Giữ số liệu chi tiết trong báo cáo/JSON; bộ nhớ chỉ lưu bối cảnh và phát hiện quan trọng.
- Kiểm tra lại toolchain nếu công việc tiếp theo cần Rust, Go hoặc .NET: tình trạng môi trường có thể thay đổi.
