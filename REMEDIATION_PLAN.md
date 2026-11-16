# Kế hoạch chỉnh sửa sau review 6 giai đoạn

Ngày 14/11/2026. Căn cứ: `bmssp_comparison/SIX_STAGES_REVIEW.md`, `bmssp_visualizer/FIDELITY_REVIEW.md` và phản ví dụ `bmssp_visualizer/exports/fidelity-near-tie.json`.

Trạng thái: **hoàn thành phạm vi chỉnh sửa 100%** ngày14/11/2026. Bằng chứng tại `REMEDIATION_RESULTS.md`. Các nghĩa vụ chứng minh tổng quát vẫn OPEN / NOT CERTIFIED, theo tiêu chí GĐ3; không coi hoàn thành sửa lỗi là chứng nhận định lý. Tiến độ này riêng với phạm vi demo cũ.

## Mục tiêu nghiệm thu

Bàn giao repo có demo BMSSP/Dijkstra hoạt động, kết quả và đường đi kiểm tra được, phép đo mô tả đúng công việc thực hiện, lệnh kiểm tra phản ánh đúng PASS/FAIL, và tài liệu phân biệt rõ kiểm thử thực nghiệm với chứng minh lý thuyết. Không đặt mục tiêu bắt buộc BMSSP phải nhanh hơn Dijkstra. Không hứa đúng trên mọi số thực bằng phép tính double.

Giữ nguyên các checkout upstream và số liệu lịch sử. Mọi sửa solver nằm trong bản sao/fork có provenance và diff; bộ đo mới dùng thư mục/version kết quả mới. Đọc lại trạng thái file trước khi sửa để tránh ghi đè công việc đồng thời.

## Giai đoạn và trọng số

| Giai đoạn | Trọng số | Tổng khi đạt | Trạng thái |
|---|---:|---:|---|
| 1. Sửa đường đi, hợp đồng số học và verifier | 25% | 25% | Đạt regression và kiểm tra exact/tolerance |
| 2. Sửa luồng kiểm tra và lệnh tái lập | 15% | 40% | Quick/full PASS; 26 test launcher |
| 3. Xác minh fidelity và giới hạn độ phức tạp | 25% | 65% | Sửa khác biệt xác nhận; nghĩa vụ chứng minh còn mở công khai |
| 4. Chuẩn hóa benchmark và thống kê transform | 25% | 90% | 30 cấu hình × 3 mode, 630 mẫu PASS |
| 5. Tích hợp demo, tài liệu và nghiệm thu | 10% | 100% | 57 test, quick/full, Chrome và offline export đạt |

## 1. Sửa tính đúng đầu ra — 25%

Phạm vi: `native/runner.cpp`, bộ verifier trong `data.py`, native/app regression, hợp đồng kết quả.

- Chốt miền số học: đồ thị trọng số không âm, hữu hạn; tách chế độ kiểm tra chính xác trên số nguyên có tổng đường đi biểu diễn chính xác với double và chế độ floating-point có dung sai công khai.
- Thêm regression thất bại trước khi sửa cho ca 0→1=4e-11, 0→2=0, 2→1=0. Kiểm tra cả distances, cạnh predecessor và tổng giá đường đi, không chỉ mảng khoảng cách.
- Kiểm tra raw predecessor của solver: đi về nguồn, không chu trình, cạnh hợp lệ, chi phí phù hợp. Ưu tiên giữ predecessor thật nếu đạt; nếu không đạt, tìm nguyên nhân trong cập nhật solver. Không chuyển sang rawpred một cách mù quáng hoặc che lỗi bằng cây BFS dùng epsilon.
- Nếu cần dựng cây riêng phục vụ xuất, công khai nguồn gốc cây và dùng tiêu chí tương thích số học đã định nghĩa; lỗi không dựng được cây phải báo thất bại, không âm thầm xuất cây gần đúng như kết quả chính xác.
- Tách trạng thái kiểm tra khoảng cách, đường đi, exact/tolerance; không để `exact_mismatches=0` bị hiểu là cây đã được chứng nhận.
- Thử source khác 0, cạnh 0, zero cycles, tie, cạnh song song, self-loop, đỉnh không tới được, một đỉnh, near-tie và số có độ lớn khác nhau. Dùng Bellman–Ford/số nguyên chính xác cho tập kiểm thử phù hợp để tránh chỉ so hai bản cùng Dijkstra float.

**Đạt khi:** ca phản ví dụ xuất đường 0→2→1 giá 0; đường sai bị verifier từ chối ở chế độ chính xác; các regression liên quan đạt; UI/export diễn đạt đúng mức kiểm chứng. Không dùng kết quả này làm chứng minh đúng tổng quát.

## 2. Sửa launcher và tính tái lập — 15%

Phạm vi: `run_all.py`, `run_fidelity_checks.py` và kết quả kiểm tra có cấu trúc.

- Mỗi bước trả PASS/FAIL/SKIPPED cùng exit code; fail bước bắt buộc làm lệnh kiểm tra kết thúc khác 0. Demo-only/skip không in đã kiểm tra hoặc 100% nghiệm thu.
- Thiếu tool/script không được coi PASS. Cache chỉ là kết quả lịch sử; phải đọc nội dung, nguồn/version/config và phân biệt rõ với kiểm tra vừa chạy.
- Parse thật `--limit`, seed toàn bộ lựa chọn n/m/trọng số; ghi seed, cấu hình, commit/diff, compiler, lệnh và lỗi vào JSON.
- Bỏ hardcode đường dẫn `C:/ttss` để chạy được từ vị trí repo khác. Tách lệnh kiểm tra khỏi mở trình duyệt; giữ lựa chọn mở demo rõ ràng.
- Test các nhánh: thiếu công cụ, child exit nonzero, timeout, JSON sai, kết quả accuracy=false, thiếu script, quick, demo-only và thành công.

**Đạt khi:** lỗi bắt buộc luôn lan tới exit code và summary; không có PASS từ mere file existence; quick chạy đúng số ca; chạy cùng seed tái tạo cùng input; không cần chạy benchmark lớn để test launcher.

## 3. Fidelity và miền áp dụng — 25%

Phạm vi: bản C++ đang dùng, Rust BlockDS, PDF gốc, báo cáo mapping và test bất biến.

- Lập bảng từng yêu cầu Algorithms 1–3/Lemma 3.3: vị trí code, khác biệt, bằng chứng, còn mở. Trích đúng PDF và tài liệu repo; sửa nhầm nguồn “§7 What this is not”.
- C++: kiểm tra Wi là tập nhưng active là vector; root propagation so với rừng F cuối; lazy heap và uniqueness; batchPQ bounds/block sizes/duplicate keys; rủi ro iterator mặc định. Chỉ kết luận lỗi khi có phân tích đủ hoặc phản ví dụ hợp lệ; sửa và thêm regression cho lỗi xác nhận.
- Kiểm tra các hợp đồng S, B, B', U, pivot coverage và disjointness trong chế độ audit. Dùng đồ thị nhỏ/oracle để kiểm tra điều kiện mà solver thường không tính trực tiếp.
- Quy định chế độ đồ thị bậc hằng và đồ thị tùy ý. Kiểm tra biến đổi, ánh xạ ID/source/predecessor, cạnh song song/self-loop/đỉnh cô lập; không chỉ bật `prepare_graph(true)` vì đã có hàm.
- Rust: công khai randomized quickselect, hash và vector shifting. Không nhận cận worst-case tất định của bài cho bản này nếu chưa có chứng minh phù hợp. Giữ Rust làm đối chiếu thực nghiệm nếu phù hợp; không bắt buộc viết lại toàn bộ Rust để hoàn thành phạm vi sửa này.
- Với từng implementation, ghi rõ một trong các mức: test kết quả đạt; mapping đã review nhưng còn giả thiết; cận riêng đã lập luận; hoặc chưa xác nhận. Các vấn đề chứng minh chưa đủ bằng chứng phải giữ OPEN/NOT CERTIFIED, không đóng bằng số test.

**Đạt khi:** các khác biệt được phân loại có bằng chứng, lỗi xác nhận được sửa/test; miền áp dụng và những cận chưa chứng nhận hiển thị trung thực. Nếu mục tiêu chuyển thành chứng nhận đầy đủ định lý cho một implementation, lập phạm vi chứng minh riêng; không tự ghi đạt chỉ vì đã hoàn thành bảng mapping.

## 4. Benchmark và transform — 25%

- Định nghĩa hai phép đo: (a) solver-only trên biểu diễn đã chuẩn bị, (b) tổng xử lý từ đồ thị đã parse tới kết quả cùng loại. Tách setup/transform, solve, dựng đường đi và kiểm chứng; file I/O, JSON, render, trace/ACK không trộn vào solver-only.
- Với API không tách được pha, gắn nhãn tổng API thay vì gọi core-only; chỉ so trực tiếp các scope tương đương. Giữ distances-only và distances+path là hai hợp đồng riêng.
- Xuất n', m', bậc lớn nhất và ánh xạ từ đồ thị thực đã transform của từng solver. Không dùng 2m/3m như kích thước đo thực. Kiểm tra bảo toàn khoảng cách và đường đi.
- Đo bật/tắt transform trên cùng implementation/cấu hình để cô lập yếu tố. So bmssp với bmssp-fast chỉ là so biến thể, không quy toàn bộ tỷ số cho transform.
- Chạy smoke correctness trước, sau đó đo tuần tự, 2 warm-up + 7 mẫu mỗi cấu hình, đổi thứ tự solver theo vòng. Lưu toàn bộ mẫu, trung vị và độ phân tán.
- Ma trận khởi đầu: chain, grid, random bounded-degree, hub/scale-free, đồ thị dày vừa; nhiều size và ít nhất 3 seed cho họ ngẫu nhiên. Đặt giới hạn thời gian/bộ nhớ trước khi tăng size; timeout/error phải có trong kết quả. Ca near-tie/zero-ties ưu tiên correctness, không dùng để xếp hạng bản biết sai.
- Có bộ đếm thao tác ở chế độ riêng (relax, heap, pivot, Pull/Insert/BatchPrepend), không trộn overhead profiling vào thời gian chuẩn. Cache chỉ là giả thuyết nếu chưa có hardware profiling; không bắt buộc triển khai profiler phần cứng trong phạm vi này.

**Đạt khi:** bảng thời gian truy được tới raw samples và phạm vi rõ ràng; số transform lấy thực; input/seed/compiler/provenance đầy đủ; cấu hình sai kết quả bị loại khỏi xếp hạng; không ngoại suy ngưỡng BMSSP thắng từ vài điểm đo.

## 5. Tích hợp và bàn giao — 10%

- UI: kiểm tra hai solver trên cùng graph/source; trạng thái exact/tolerance rõ, không dùng animation time để so tốc độ. Nhãn trace, cây xuất và thuật toán đúng provenance.
- Chạy test phù hợp thay đổi, kiểm tra browser live/pause/step, đổi input, xuất/import HTML/JSON/CSV và tái hiện đường đi. Không yêu cầu chạy lại toàn bộ benchmark nếu chỉ đổi tài liệu/UI.
- Viết lại kết luận các báo cáo 6 giai đoạn theo evidence; giữ số liệu cũ với nhãn lịch sử. Loại tuyên bố không nguồn, bảo đảm tuyệt đối, nguyên nhân cache chưa đo và ngưỡng thắng giả định như sự thật.
- README một luồng rõ: cài/build → kiểm tra → demo → benchmark → đọc kết quả. Lệnh nghiệm thu tạo manifest tổng hợp PASS/FAIL/SKIPPED và vấn đề còn mở.
- Cập nhật memory, kế hoạch và danh sách issue. Chỉ đóng lỗi sau regression và xác nhận code thực thi đã cập nhật.

**Đạt khi:** từ môi trường có dependency đã ghi, người khác chạy được luồng đã tài liệu hóa; kết luận không vượt quá bằng chứng; còn vấn đề chứng minh nào đều công khai.

## Phân công và thứ tự

Nếu dùng subagent theo ủy quyền đã có: Terra phụ trách solver/fidelity, Sol phụ trách launcher/API/UI contract, Luna phụ trách đối chiếu tài liệu/evidence và regression có phạm vi rõ; root kiểm tra độc lập và tích hợp. Không cho nhiều agent sửa cùng file đồng thời. GĐ1 và GĐ2 có thể song song; GĐ3 đọc/audit song song, nhưng GĐ4 chỉ chốt số liệu sau khi solver và hợp đồng đầu ra ổn định; GĐ5 cuối cùng. Benchmark chạy tuần tự, không tranh CPU với build/test nặng.

Báo tiến độ theo các mốc trên, cập nhật tối đa khoảng 60 giây khi đang làm. Kế hoạch hoàn thành không tăng tiến độ triển khai. Mọi mở rộng sang viết lại cấu trúc dữ liệu hoặc chứng minh hình thức phải được phân biệt với phạm vi sửa hiện tại.
