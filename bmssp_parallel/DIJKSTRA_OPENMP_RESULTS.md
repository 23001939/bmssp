# Kết quả Dijkstra OpenMP — cập nhật 20/11/2026

## Đo kiểm v2: wall time, quét tuần tự, prepared SSSP, hai toolchain

Đã hoàn tất năm yêu cầu bổ sung. Kết quả v1 phía dưới là lịch sử, không dùng thay cho phép so v2. Mã được refactor thành snapshot `PreparedDijkstraGraph` dùng chung cho bản quét tuần tự thuần và OpenMP; API cũ vẫn hoạt động. Dữ liệu gốc không bị sửa, cạnh trùng được gộp theo trọng số nhỏ nhất. Bản quét tuần tự không chứa OpenMP construct; OMP 1 đi cùng đường mã song song như OMP 2/4/8, không có nhánh solver riêng.

**Timer:** cả v1 và v2 dùng `std::chrono::steady_clock`, bắt đầu/kết thúc trên main thread gọi solver; không dùng `clock()` hoặc cộng CPU time các luồng. V2 thêm `static_assert(Clock::is_steady)`. Theo [OpenMP 5.1 về omp_get_wtime](https://www.openmp.org/spec-html/5.1/openmpsu185.html), timer OpenMP cũng trả elapsed wall time nhưng giá trị giữa các luồng không nhất thiết đồng nhất; cách đặt hai mốc trên cùng luồng là cần thiết nếu dùng nó.

**Phạm vi đo độc lập:**

- `api`: toàn bộ lời gọi trên CSR gốc; scan/OpenMP gồm validation, cấp phát và chuẩn hóa, tính SSSP, dọn CSR tạm trước khi trả về. Heap cũ không validate/chuẩn hóa.
- `sssp_prepared`: dùng chung snapshot đã validate/chuẩn hóa; vẫn gồm cấp phát, khởi tạo và dọn workspace, cấp phát kết quả, vào/ra parallel region. Đây là lời gọi SSSP sau chuẩn bị, chưa phải kernel loại bỏ mọi cấp phát.
- `preparation`: constructor snapshot riêng, không gồm hủy snapshot. Không lấy API trừ preparation để tính SSSP. Các median độc lập không có tính cộng tuyến; cache, allocator, tải hệ thống và thứ tự đo ảnh hưởng số đo.
- Mọi scope loại trừ tạo đồ thị, I/O, kiểm chứng và hủy kết quả trả về. Speedup lấy **scan tuần tự cùng scope** chia cho thời gian mode đang xét; vẫn giữ heap API làm đối chứng sử dụng thực tế.

Mỗi compiler chạy tuần tự 4 đồ thị, 11 mode, 2 warm-up và 7 mẫu/mode, xoay thứ tự; seed `20261119`. Cả **396 lời gọi/compiler** khớp chính xác toàn bộ khoảng cách. Đã giữ mọi mẫu thời gian, compiler/version/flags, môi trường và SHA-256 nguồn trong JSON.

| Compiler | Đồ thị n=1000 | Preparation (ms) | Scan API | OMP4 API | Scan prepared | OMP1 prepared | OMP4 prepared |
|---|---|---:|---:|---:|---:|---:|---:|
| GCC 16.2 / libgomp | Thưa, m=9000 | 0,1470 | 3,4168 | 140,4710 | 2,5974 | 2,9780 | 155,8709 |
| GCC 16.2 / libgomp | Dày, m=1000000 | 47,1531 | 16,4874 | 111,2058 | 1,7754 | 2,3028 | 89,6442 |
| LLVM-MinGW 23.1.5 / libomp | Thưa, m=9000 | 0,1566 | 1,4762 | 49,0839 | 1,2388 | 1,4588 | 47,9684 |
| LLVM-MinGW 23.1.5 / libomp | Dày, m=1000000 | 32,9459 | 47,8476 | 322,2210 | 5,0564 | 6,2750 | 257,4515 |

Các số là median wall-clock ms. Các lượt đo có độ phân tán đáng kể: ví dụ GCC scan prepared trên đồ thị thưa 1000 đỉnh dao động 1,092–9,854 ms; LLVM OMP4 prepared trên đồ thị dày dao động 207,937–304,915 ms. Không xếp hạng tốc độ compiler từ các phiên này; chưa kiểm soát toàn bộ tải máy/nhiệt/xung CPU. Dù đã bỏ chuẩn bị và dùng scan tuần tự đúng cùng scope, **không quan sát tăng tốc với 2/4/8 luồng trên cả bốn đồ thị của mỗi toolchain**.

**Tổ chức luồng và độ hạt:** rà mã xác nhận một `omp parallel` bao toàn bộ vòng chọn đỉnh, không có `critical`; mỗi thread giữ local minimum rồi `single` gộp O(p). Có ba điểm barrier mỗi lượt relax để bảo đảm thứ tự pha. Trên đồ thị thưa 1000 đỉnh, trung bình 8,966 cạnh ra/đỉnh: với 4 luồng chỉ khoảng 2,24 cạnh relax và 250 đỉnh quét/luồng/lượt. Ca dày có 999 cạnh ra/đỉnh, khoảng 250 cạnh relax/luồng/lượt. Tăng kích thước công việc relax trong tập đã đo vẫn chưa tạo speedup. Công việc nhỏ và đồng bộ dày là giả thuyết phù hợp; chưa có profiling đo tỷ trọng barrier nên không khẳng định đó là nguyên nhân duy nhất. JSON có bậc trung bình/tối đa, số đỉnh/cạnh ước tính mỗi luồng và team từ probe riêng (không gắn nhãn là đo team bên trong solver).

## Runtime, hộp thoại memory và CMake/CTest

Thông báo Windows “memory could not be read” là access violation `0xc0000005` ở probe, không phải báo thiếu RAM. Probe chỉ dùng OpenMP và cũng lỗi với LLVM khi `OMP_THREAD_LIMIT=2` hoặc `OMP_DYNAMIC=TRUE`. Chưa đủ bằng chứng xác định chính xác lỗi compiler/runtime/cấu hình nạp DLL; đổi trạng thái v2 thành **`SKIPPED_RUNTIME_FAILURE`**. Không coi việc tắt hộp thoại là sửa lỗi nền. Runner và probe đặt Windows error mode trong phạm vi tiến trình để crash trả mã lỗi, không bật modal chặn người dùng; không sửa cài đặt hệ thống.

Hai phép đo mới đều bỏ yêu cầu affinity (`--affinity none`) vì libgomp Windows báo không hỗ trợ affinity khi yêu cầu `close`. Runner loại bỏ biến OMP/KMP/GOMP kế thừa, prepend compiler vào PATH, build riêng theo compiler. LLVM ghi nhận DLL thực nạp tại `D:\TTSS_CK\.tools\llvm-mingw-20261104-ucrt-x86_64\bin\libomp.dll`, SHA-256 `ee4fa811b4c66a4bb67bc0764de3352e4f41cfb5cbc201f95b2d9979f57d633e`. GCC binary không import DLL OpenMP (PE chỉ có KERNEL32 và msvcrt), phù hợp bộ libgomp liên kết tĩnh của w64devkit. Đã lưu module/probe và PE imports, không suy từ PATH rằng DLL đó chắc chắn được nạp.

| Bộ công cụ | Thông thường | Giới hạn team=2 | Dynamic team | CMake configure/build | CTest thông thường |
|---|---|---|---|---|---|
| GCC 16.2.0 / libgomp | PASS | PASS | PASS | PASS | 3/3 PASS |
| LLVM-MinGW 23.1.5 / libomp | PASS | SKIPPED_RUNTIME_FAILURE | SKIPPED_RUNTIME_FAILURE | PASS | 3/3 PASS |

Mỗi cấu hình solver PASS đạt **279.132 checks / 1.943 full-result comparisons**; bao gồm scan/API/prepared, reuse nhiều nguồn, predecessor khớp scan, input không bị sửa, các ca biên và 120 đồ thị ngẫu nhiên. CTest gồm runtime probe bình thường, hạ tầng Người 1 (153.932 checks) và Dijkstra mở rộng. GCC dynamic/limited cũng chạy toàn bộ solver tests, không chỉ probe. Chưa chạy ThreadSanitizer/OpenMP race detector.

Portable GCC, CMake 4.5.0, Ninja 1.13.2 nằm dưới `D:\TTSS_Prj\.tools`, không cài toàn hệ thống. Archive SHA-256 khớp official release digests. Các bằng chứng:

- [GCC v2: mẫu thời gian, probes, metadata](results/dijkstra_openmp_v2_gcc.json).
- [LLVM v2: mẫu thời gian, module thực nạp, probes](results/dijkstra_openmp_v2_llvm.json).
- [Log CMake/CTest GCC](results/cmake_ctest_gcc.json), [log LLVM](results/cmake_ctest_llvm.json).
- [URL/hash và phiên bản portable tools](results/portable-toolchain-provenance.json).

Tái lập từ repo root:

```powershell
python bmssp_parallel/run_dijkstra_openmp.py --cxx D:\TTSS_Prj\.tools\w64devkit\bin\g++.exe --affinity none --output bmssp_parallel/results/dijkstra_openmp_v2_gcc.json
python bmssp_parallel/run_dijkstra_openmp.py --cxx D:\TTSS_CK\.tools\llvm-mingw-20261104-ucrt-x86_64\bin\g++.exe --affinity none --output bmssp_parallel/results/dijkstra_openmp_v2_llvm.json
```

Để tái lập CMake, thêm thư mục compiler và `.tools\ninja` vào PATH, gọi `.tools\cmake-4.5.0-windows-x86_64\bin\cmake.exe` với `-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=<absolute-cxx-path>`; build và chạy `ctest --test-dir <build-dir> --output-on-failure`. Lệnh chính xác, môi trường test và output đã lưu trong từng log JSON. CMake verification dùng flags project gồm `-march=native`; timing harness dùng flags trong JSON không có `-march=native`, nên không trộn thời gian test CMake vào benchmark.

## Kết quả v1 lưu lịch sử — 19/11/2026

Đã tích hợp `dijkstra_openmp(graph, source, threads)` vào `bmssp_core`, dùng CSR và `SSSPResult` hiện có. Giữ nguyên heap Dijkstra tuần tự làm oracle. Phân tích nguồn, giấy phép và đồng bộ: [DIJKSTRA_OPENMP_REVIEW.md](DIJKSTRA_OPENMP_REVIEW.md). Cách gọi/build: [README.md](README.md).

## Kiểm chứng

- **136.505 kiểm tra PASS, 836 lần đối chiếu toàn bộ kết quả**; toàn bộ distances khớp chính xác baseline heap. Bộ ngẫu nhiên gồm 120 đồ thị, seed `20261119`; Bellman–Ford độc lập bổ sung đối chiếu cho đồ thị nhỏ (có dung sai tương đối 1e-12 trên số thực do thứ tự cộng khác). Có kiểm tra predecessor bằng cạnh thật và khoảng cách, không chu trình; không ép predecessor giống oracle khi hòa.
- Gồm mẫu sáu đỉnh nguyên bản FSU với đáp án `[0,35,15,45,49,41]`; đồ thị rỗng, một đỉnh, cô lập, nguồn khác 0/ngoài đồ thị, chu trình 0, self-loop, cạnh trùng, near-tie 1e-11, khoảng cách vượt 100000, tổng tràn double và đầu vào sai.
- Chạy mặc định/1/2/4/8 luồng và lặp lại; kiểm tra nested parallel với `max_active_levels=1` yêu cầu tám nhưng nhận một luồng.
- Hạ tầng cũ: **153.932/153.932 kiểm tra PASS** khi chạy `build_and_test.py` sau tích hợp.
- Benchmark kiểm tra lại toàn bộ khoảng cách ở cả 180 lời gọi, gồm warm-up: **đều khớp chính xác**.

Hai cấu hình **không được tính PASS**: `OMP_THREAD_LIMIT=2`, `OMP_DYNAMIC=TRUE`. Probe độc lập chỉ có `omp parallel`, không chứa mã Dijkstra, cũng lỗi truy cập bộ nhớ `0xc0000005` với runtime LLVM-MinGW hiện tại. Runner lưu stderr, exit code và đánh dấu `SKIPPED_RUNTIME_UNSUPPORTED`; không chạy solver trong các cấu hình đó. Đây là giới hạn kiểm chứng của môi trường hiện tại, chưa chứng nhận solver trên hai cấu hình này. Chưa chạy ThreadSanitizer hay OpenMP race detector; rà soát tĩnh và stress hữu hạn không chứng minh tổng quát không có race.

## Đo tại máy hiện tại

Windows x86-64, Intel Core i9-13900H, 20 CPU logic. LLVM-MinGW Clang 23.1.5, C++20, `-O3 -mcx16 -fopenmp -Wall -Wextra`, không fast-math. Team thực tế lần lượt 1/2/4/8; `OMP_DYNAMIC=FALSE`, `OMP_PROC_BIND=close`, `OMP_PLACES=cores`. Không chạy các benchmark đồng thời. Mỗi mode có 2 warm-up và 7 mẫu; xoay thứ tự mode, báo median. Dữ liệu thô, phiên bản compiler đầy đủ, trạng thái Git và hash toàn bộ nguồn/header: [results/dijkstra_openmp.json](results/dijkstra_openmp.json), thời điểm 21:54 ngày 19/11/2026 (Asia/Saigon). Commit nền `874775b`; các thay đổi mới chưa commit, được nhận diện bằng SHA-256.

| Đồ thị | n | m | Heap tuần tự (ms) | OMP 1 (ms) | OMP 2 (ms) | OMP 4 (ms) | OMP 8 (ms) |
|---|---:|---:|---:|---:|---:|---:|---:|
| Thưa | 100 | 900 | 0,0205 | 0,0340 | 3,0339 | 4,0319 | 8,3485 |
| Thưa | 1000 | 9000 | 0,2888 | 1,1696 | 32,3990 | 45,6120 | 78,6028 |
| Dày | 100 | 10000 | 0,0565 | 0,1483 | 3,0778 | 4,1867 | 7,6824 |
| Dày | 1000 | 1000000 | 2,1640 | 13,9180 | 60,0541 | 79,2029 | 149,2956 |

Đồ thị có hướng, nguồn 0; vòng nối bảo đảm reachable rồi thêm cạnh theo seed `20261119+n`, trọng số nguyên [0,99]. Đồ thị dày có cạnh trùng với vòng nối, nên m gồm cả cạnh song song. Cả hai solver nhận cùng CSR gốc, bản OpenMP tự chuẩn hóa trong mỗi lời gọi.

Thời gian bao gồm cấp phát kết quả, validation/chuẩn hóa của OpenMP, khởi tạo nhóm và tính SSSP; loại trừ sinh đồ thị, I/O, kiểm chứng và hủy kết quả. Heap hiện có không validate CSR. Không đo riêng setup/solve nên không quy toàn bộ chênh lệch cho một yếu tố.

**Không có tăng tốc trên bốn đồ thị đã đo.** Bản OMP 4 luồng chậm khoảng 36,6 lần heap trên ca dày 1000 đỉnh. Dạng quét có O(n²) công việc chọn min, khác heap O((n+m)log n); chuẩn hóa và barrier mỗi đỉnh cũng thêm chi phí. Các yếu tố này phù hợp với kết quả, nhưng chưa có profiling để định lượng đóng góp riêng. Không ngoại suy thành kết luận cho mọi máy, đồ thị hoặc quy mô. Giữ heap làm baseline hiệu năng; bản OpenMP dùng nghiên cứu song song hóa và kiểm chứng đồng bộ, chưa nên thay mặc định hay chạy trên đồ thị đường bộ triệu đỉnh với dạng quét này.

## Tái lập

Từ thư mục gốc repo:

```powershell
python bmssp_parallel/run_dijkstra_openmp.py --cxx D:\TTSS_CK\.tools\llvm-mingw-20261104-ucrt-x86_64\bin\g++.exe
```

Đổi `--cxx` sang compiler OpenMP phù hợp trên máy khác. Runner build từ nguồn, tự chạy probes, tests rồi benchmark; ghi đè file kết quả mặc định. CMake đã thêm các target và CTest, nhưng phiên này xác minh bằng lệnh compiler trong hai Python harness, chưa chạy CMake/CTest do CMake không có trên PATH.
