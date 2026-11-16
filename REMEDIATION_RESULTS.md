# Kết quả chỉnh sửa

Ngày 14/11/2026. Đối chiếu `REMEDIATION_PLAN.md`. Các bản sửa nằm ngoài checkout upstream; báo cáo cũ giữ nhãn lịch sử.

## Những thay đổi đã kiểm tra

- Runner xác minh raw predecessor và ưu tiên xuất cây solver; fallback chỉ dùng cạnh khớp binary64 chính xác, có `predecessor_origin`, báo lỗi nếu không dựng được cây. Ca near-tie đã ra `[null,2,0]`, giá đường đi tới 1 bằng0. Bằng chứng `bmssp_visualizer/exports/remediation/near-tie-fixed.json`.
- Verifier có mode auto/exact/tolerance; kiểm tra riêng khoảng cách/cây, chu trình, cạnh và tổng chi phí. Miền exact auto giới hạn vào trọng số nguyên có tổng đường đơn an toàn trong double. UI và export công khai mức kiểm tra.
- Bản C++ sao chép khử trùng frontier theo Wi là tập, dùng marker khi tra upper bounds thay iterator mặc định, và cấp node riêng cho đỉnh cô lập trong transform. Nhánh transform bỏ self-loop không âm. Có regression debug iterator, ánh xạ, nguồn cô lập, cạnh song song và đường đi.
- Launcher truyền FAIL/exit code, không lấy file tồn tại làm PASS, parse thật limit/seed, ghi manifest và provenance. Quick/full có phạm vi rõ; mở demo là lựa chọn riêng. Bộ fidelity dùng oracle Bellman–Ford và ghi đúng dung sai; lỗi đã biết của pristine upstream được liệt kê diagnostic không bắt buộc.
- Benchmark v2 lưu setup/solve/tổng xử lý, raw samples, kích thước transform thực, mapping và profile thao tác riêng. `bmssp_comparison/BENCHMARK_V2_REPORT.md` dẫn tới kết quả cuối30cấu hình×3mode; 630mẫu sau warm-up đều đạt exact distances+raw-path trên dữ liệu nguyên.
- Đã hiệu chỉnh các tài liệu lịch sử, thêm README luồng sử dụng và khởi động lại server cục bộ8765 để nạp code mới.

## Bằng chứng nghiệm thu

- 21 test data/app/API, 10 test native, 26 test launcher/fidelity mocked đã đạt trong lượt tích hợp. Launcher quick/full cuối chạy lại các regression theo manifest; số test thực được ghi trong stdout/stderr.
- Manifest nghiệm thu: `bmssp_comparison/remediation/launcher-final-quick.json` và `launcher-final-full.json`, đều PASS/exit0; mỗi lượt chạy lại47 test app/launcher và10 native. Full đối chiếu20 input × C++/Rust =40 ca solver đạt. Diagnostic pristine C++ near-tie vẫn FAIL đúng như lỗi upstream đã biết, required=false; không che lỗi hoặc dùng nó làm kết quả production.
- Chrome headless chạy hai solver cùng graph hash, kiểm tra nhãn exact và mở HTML ngoại tuyến không JS exception. Kết quả: `bmssp_visualizer/exports/remediation/browser.json`; ảnh, HTML/JSON/CSV hai thuật toán cùng thư mục. Tests API/native kiểm tra controlled pause/step/trace và các ca đặc biệt.
- Kết quả benchmark cuối: `bmssp_comparison/results_v2/20261114-092651/results.json`. Các lượt smoke/trial trước đó giữ để truy vết, không dùng làm số liệu chính.

## Các vấn đề vẫn OPEN / NOT CERTIFIED

Chưa có chứng minh đầy đủ root propagation tương đương rừng F cuối cùng, mọi điều kiện complete/disjoint/coverage trên mọi lời gọi, hoặc amortized/worst-case của toàn bộ batchPQ. Test trace nhỏ và dữ liệu oracle chỉ hỗ trợ một số bất biến. Rust dùng random quickselect và vector shifting nên không nhận cận tất định worst-case của bài; Rust tiếp tục là bản đối chiếu thực nghiệm, không viết lại trong phạm vi này. Miền dữ liệu bậc tùy ý trong UI vẫn chạy không transform và không được gắn bảo đảm bậc hằng. Benchmark transform là chế độ riêng có test.

Exact binary64 không phải số thực chính xác tùy ý. Bộ đo mới không đo lại Rust lớn, không đo cache phần cứng hoặc chứng minh ngưỡng BMSSP vượt Dijkstra. Việc hoàn tất sửa lỗi có phạm vi không đồng nghĩa đóng các nghĩa vụ chứng minh này.

## Chạy lại

```powershell
python run_all.py --quick
python run_all.py
python bmssp_visualizer/start_demo.py --dataset weighted_100 --no-autoplay
python bmssp_comparison/benchmark_v2.py
```

Full yêu cầu executable Rust đối chiếu đã build; thiếu dependency phải FAIL và mô tả lỗi, không tự coi đã kiểm tra. Xem README để phân biệt cài/build, kiểm tra, demo và benchmark.
