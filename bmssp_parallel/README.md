# BMSSP parallel: hạ tầng và baseline Dijkstra OpenMP

Baseline mới dùng trực tiếp `CSRGraph` và trả `SSSPResult`, cùng giao diện dữ liệu với `dijkstra_sequential`. Đây là Dijkstra dạng quét song song; chưa phải triển khai BMSSP song song. Phân tích hai nguồn, các lỗi đồng bộ và giấy phép nằm trong [DIJKSTRA_OPENMP_REVIEW.md](DIJKSTRA_OPENMP_REVIEW.md). Snapshot nguyên bản, MIT notice và SHA-256 nằm trong [references/dijkstra_openmp](references/dijkstra_openmp/README.md).

```cpp
#include "baseline/dijkstra_openmp.hpp"
auto graph = bmssp::CSRGraph::from_edge_list(3, {{0, 1, 2}, {1, 2, 3}});
auto result = bmssp::dijkstra_openmp(graph, 0, 4); // dist = {0, 2, 5}
// Reuse validation/normalization for repeated SSSP calls:
bmssp::PreparedDijkstraGraph prepared(graph);
auto serial_scan = bmssp::dijkstra_scan_prepared(prepared, 0);
auto parallel_core = bmssp::dijkstra_openmp_prepared(prepared, 0, 4);
```

Ví dụ chạy trực tiếp sáu đỉnh: [examples/demo_dijkstra_openmp.cpp](examples/demo_dijkstra_openmp.cpp). CMake target `demo_dijkstra_openmp` in bảng khoảng cách tuần tự/OpenMP từng đỉnh và trạng thái khớp. Phiên 19/11/2026 đã build/chạy thành công với bốn luồng yêu cầu, kết quả `[0,35,15,45,49,41]`. Đây là demo kiểm tra kết quả; giao diện `bmssp_visualizer` hiện vẫn chỉ có BMSSP và Dijkstra tuần tự.

`threads=0` dùng cấu hình mặc định OpenMP. Nguồn ngoài đồ thị trả mảng khoảng cách vô cực và `NO_PRED` như baseline tuần tự. CSR sai cấu trúc, cạnh âm/NaN/vô cực hoặc số luồng âm gây `std::invalid_argument`. Tổng vượt miền `double` giữ vô cực; không phân biệt được đường vượt miền số với đỉnh không tới được. Không bật `-ffast-math`.

Một nhóm luồng được giữ xuyên suốt solver. Mỗi lượt gồm tìm cực tiểu cục bộ → barrier → một luồng gộp/chốt đỉnh → barrier ngầm → relax cạnh → barrier ngầm. Chuẩn hóa các cạnh trùng `(u,v)` bằng trọng số nhỏ nhất bảo đảm mỗi đích chỉ có một luồng ghi `dist/pred`. Đồ thị đầu vào không bị thay đổi. Xử lý hòa khoảng cách bằng ID đỉnh nhỏ nhất và chỉ relax khi giảm nghiêm ngặt.

## Build, kiểm chứng và đo thời gian

Yêu cầu compiler C++20 có OpenMP (GNU hoặc Clang với runtime phù hợp), Python 3.11+. Chạy từ thư mục gốc repo:

```powershell
python bmssp_parallel/run_dijkstra_openmp.py --cxx g++
```

Máy hiện tại có LLVM-MinGW ở đường dẫn sau; có thể dùng compiler khác qua `--cxx`:

```powershell
python bmssp_parallel/run_dijkstra_openmp.py --cxx D:\TTSS_CK\.tools\llvm-mingw-20261104-ucrt-x86_64\bin\g++.exe
```

Runner thêm thư mục compiler vào PATH, xóa các biến OpenMP kế thừa rồi đặt cấu hình rõ ràng, tách thư mục build theo compiler. Kết quả v2 mặc định lưu tại `results/dijkstra_openmp_v2.json`; dùng `--output` để giữ riêng từng toolchain. `--affinity none` bỏ yêu cầu ghim CPU, phù hợp phép so sánh hai bộ runtime trên máy này vì libgomp Windows không hỗ trợ affinity. Không cần gói Python ngoài. Windows error dialogs chỉ bị tắt trong runner và tiến trình con; probe crash vẫn được ghi mã lỗi, không biến thành PASS.

Nếu có CMake 3.20+ và OpenMP trên PATH:

```powershell
cmake -S bmssp_parallel -B bmssp_parallel/build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build bmssp_parallel/build/cmake --config Release
ctest --test-dir bmssp_parallel/build/cmake -C Release --output-on-failure
```

`build_and_test.py` vẫn kiểm tra hạ tầng Người 1; runner mới chuyên kiểm tra baseline OpenMP. Các ví dụ nguồn tham khảo không được liên kết vào sản phẩm.

## Cách đọc kết quả

Cả hai phạm vi dùng `std::chrono::steady_clock`, bắt đầu/kết thúc trên luồng gọi. `api` đo toàn bộ lời gọi trên CSR gốc, gồm validation/chuẩn hóa và dọn dữ liệu tạm của scan/OpenMP. `sssp_prepared` đo lời gọi trên cùng snapshot đã chuẩn bị, vẫn gồm cấp phát/khởi tạo kết quả và workspace, vào/ra team; loại trừ validation/chuẩn hóa. Preparation được đo riêng qua constructor, không lấy API trừ setup để suy ra lõi. Mọi phạm vi loại trừ tạo đồ thị, I/O, kiểm chứng và hủy kết quả trả về.

Mốc `scan` là vòng quét tuần tự thuần, không có OpenMP construct; `speedup_vs_scan` luôn so cùng phạm vi. Heap vẫn là đối chứng thực tế và không validate CSR. Quét chọn min có O(n²) công việc, heap có O((n+m)log n); chuẩn hóa thêm O(Σ deg(u) log deg(u)), bộ nhớ O(n+m+p). Xem số đo thật trong [DIJKSTRA_OPENMP_RESULTS.md](DIJKSTRA_OPENMP_RESULTS.md).

Kiểm thử hữu hạn và rà soát barrier chưa phải chứng minh không có race. Những chế độ runtime không hoạt động trên máy phải được ghi rõ; không tính là kiểm thử đã qua. ThreadSanitizer/OpenMP race detector chưa được chạy trong môi trường Windows này.

Trên LLVM-MinGW 23.1.5 của máy hiện tại, probe chỉ dùng OpenMP cũng lỗi truy cập bộ nhớ `0xc0000005` khi đặt `OMP_THREAD_LIMIT=2` hoặc `OMP_DYNAMIC=TRUE`. Runner ghi `SKIPPED_RUNTIME_FAILURE`: chưa đủ bằng chứng xác định lỗi compiler, runtime hay cấu hình nạp DLL. Probe ghi module thực nạp và runner lưu đường dẫn/hash khi có; cấu hình thông thường lỗi khiến runner thất bại. Phép thử gọi lồng với `max_active_levels=1` kiểm tra solver khi nhận một luồng thay vì tám yêu cầu. Tệp [tests/openmp_runtime_probe.cpp](tests/openmp_runtime_probe.cpp) tái hiện độc lập với solver. Các kết quả lịch sử v1 được giữ nguyên để đối chiếu; thuật ngữ trạng thái mới áp dụng từ v2.
