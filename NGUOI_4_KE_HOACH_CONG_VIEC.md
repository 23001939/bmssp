# Các bước tiếp theo của Người 4 trong dự án BMSSP

Bạn phụ trách kiểm thử tính đúng, an toàn đa luồng và hiệu năng của `bmssp_parallel/`. **Đã hoàn tất kiểm thử mục 1–3 và chạy benchmark mục 4.** Nhánh `feat/person4-qa` đã tích hợp lõi của Người 3 và `ConcurrentFrontierD` của Người 2 từ commit `b3ffe2f`. Kết quả scaling tốt nhất là **1,47×** trong các lượt đo, dưới mốc 3×. Kết quả cache phần cứng L1/L3 còn thiếu do WSL không hỗ trợ các bộ đếm trên máy đo. Các file kiểm thử của Người 4 dùng C++. [Bản tích hợp `b3ffe2f`](https://github.com/23001939/bmssp/tree/b3ffe2f) · [Báo cáo mục 4](bmssp_parallel/person4_results/scaling_report.md)

[Phân công Người 4 và tiêu chí nghiệm thu](team_allocation_and_technical_workflow.md#34-người-4-kỹ-sư-kiểm-thử-threadsanitizer--tinh-chỉnh-hiệu-năng-giai-đoạn-3---làm-cuối) · [Thiết kế BMSSP song song](bmssp_parallelization_design_document.md) · [Bài báo gốc, Thuật toán 1–3 và Lemma 3.3](2504.17033v2.pdf)

## Trạng thái ngày 22/11/2026

| Mốc | Trạng thái |
| --- | --- |
| 1. Kiểm thử hạ tầng | Hoàn tất. Log và lệnh chạy lại ở mục 1. |
| 2. Kiểm thử BMSSP với `MutexFrontierD` | Đã chạy đối chứng, ca biên, cutoff và TSan cho bộ Mutex. Báo cáo ghi hai khác biệt cần Người 3 đối chiếu. |
| 3. Kiểm thử `ConcurrentFrontierD` | Hoàn tất đối chứng, stress test 32 luồng và TSan với hai cấu hình. |
| 4. Benchmark và profiling | Đã đo hai họ đồ thị từ 1 đến 10 triệu đỉnh, xuất dữ liệu và biểu đồ, quét cutoff và `M`. Tốc độ chưa đạt 3×. Cần nhóm xác nhận mốc nghiệm thu và môi trường có bộ đếm L1/L3. |

Người 2 đã báo cáo stress test **32 luồng, 10 triệu thao tác** và số lần tách block giảm **55,05%** trong ca đo Eager Cleanup. README bản tích hợp ghi **4/4** test đạt ở Release và ASan. Phạm vi hạ tầng và stress test đã có kết quả TSan, còn toàn bộ suite BMSSP song song chưa được xác nhận sạch TSan. Các ô nghiệm thu bên dưới ghi kết quả chạy của Người 4. [Báo cáo Người 2](https://github.com/23001939/bmssp/blob/1285a81/bmssp_parallel/PERSON2_CONCURRENT_D_REPORT.md) · [Kết quả bản tích hợp](https://github.com/23001939/bmssp/blob/b3ffe2f/bmssp_parallel/README.md#6-kiểm-chứng-threadsanitizer)

## 1. Làm ngay với hạ tầng Người 1

- [x] Từ thư mục gốc repo, chạy `python bmssp_parallel/build_and_test.py`. Ghi lại commit, phiên bản GCC và kết quả. Kiểm tra script thoát với mã `0` và các bộ test hạ tầng báo `PASS`. Script dùng `g++` với C++20 và OpenMP.
- [x] Dùng `dijkstra_sequential(const CSRGraph&, uint32_t)` trong [`bmssp_seq.hpp`](bmssp_parallel/include/baseline/bmssp_seq.hpp) làm đối chứng cho `tests/verify_correctness.cpp`. Chuẩn bị dữ liệu và kiểm tra phần đối chứng với các đáp án biết trước trong lúc chờ API solver song song.
- [x] Chuẩn bị `tests/test_atomic_state.cpp` và `tests/test_gadget_transform.cpp` dựa trên độ bao phủ của [`test_person1_comprehensive.cpp`](bmssp_parallel/tests/test_person1_comprehensive.cpp). Với atomic, kiểm tra kích thước/căn chỉnh 16 byte và tính nhất quán của `dist`, `pred`, `epoch` khi nhiều luồng cập nhật. Với gadget, kiểm tra bậc vào/ra tối đa 2 và khoảng cách sau khi hoàn nguyên, kể cả khi nguồn bị cô lập.
- [x] Bổ sung target cho các test đã sẵn sàng trong `bmssp_parallel/CMakeLists.txt` và đăng ký với CTest. Kết quả mong đợi là lỗi đối chứng làm test thoát với mã khác `0` và in đủ thông tin để chạy lại.

- Bộ test Người 1 đạt **153.932/153.932** kiểm tra, mã thoát `0`, với **GCC 14.2.0** tại commit `874775b976761db39d95f1288644fa732518ff5b`. [Log Người 1](bmssp_parallel/person4_results/person1_baseline.txt)
- CTest Release đạt **3/3** bộ test. `verify_correctness` kiểm tra Dijkstra trên **17 ca**, gồm 12 ca ngẫu nhiên và dày đối chiếu với Bellman–Ford. `test_atomic_state` chạy với **8 và 16 luồng ghi**, mỗi cấu hình 128 vòng. `test_gadget_transform` xác nhận bậc vào/ra tối đa 2 và khôi phục đúng khoảng cách cho cả nguồn bậc cao lẫn nguồn cô lập. [Log CTest](bmssp_parallel/person4_results/ctest_release.txt)
- Hai bản sao kiểm thử có dữ liệu cố tình sai đều thoát với mã `1` và in đỉnh lỗi. Ca near-tie dùng đáp án sai `4e-11`. Ca gadget dùng chỉ số đại diện ngoài phạm vi. [Log kiểm tra đường báo lỗi](bmssp_parallel/person4_results/ctest_release.txt)

Chạy lại ba bộ test từ thư mục gốc repo khi `cmake`, `ctest`, `ninja` và `g++` có trên `PATH`.

```powershell
cmake -S bmssp_parallel -B bmssp_parallel/build/person4 -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build bmssp_parallel/build/person4
ctest --test-dir bmssp_parallel/build/person4 --output-on-failure --no-tests=error
```

### Các ca đối chứng đã chuẩn bị

| Ca | Kiểm tra |
| --- | --- |
| Một đỉnh và đồ thị rời rạc | Nguồn có khoảng cách `0`, mọi đỉnh không tới được có khoảng cách vô cực. |
| Cạnh trọng số `0`, chu trình và hai đường đồng hạng | Khoảng cách khớp đối chứng. Nếu solver xuất đỉnh cha, đường truy ngược phải về được nguồn, không lặp đỉnh và có chi phí đúng. |
| Near-tie | Dùng `0 → 1 = 4e-11`, `0 → 2 = 0`, `2 → 1 = 0`. Kiểm tra khoảng cách đến đỉnh `1` **bằng `0`** vì dung sai `1e-9` sẽ cho qua kết quả sai `4e-11`. |
| Đồ thị có đỉnh bậc cao | Chạy Dijkstra trên đồ thị gốc và đồ thị đã biến đổi, với nguồn tương ứng. So sánh toàn bộ kết quả sau khi hoàn nguyên. |
| Đồ thị ngẫu nhiên và đồ thị dày | Đã cố định seed và đối chiếu Dijkstra với Bellman–Ford trên toàn bộ đỉnh. Dùng lại các ca này ở nhiều mức luồng khi tích hợp BMSSP ở mục 2. |

## 2. Kiểm thử lõi BMSSP với MutexFrontierD

- [x] Nối `tests/verify_correctness.cpp` với `bmssp_parallel` và chọn `FrontierType::Mutex`. Đã đối chiếu mọi đỉnh với Dijkstra ở **1, 2, 4 và 8 luồng** trên 18 ca, gồm đồ thị dày có nhiều cạnh chung đích. Cả **72 lượt** đều đạt ngưỡng sai số **`< 1e-9`**. Ca near-tie khớp chính xác.
- [x] Kiểm tra các cận `B′ᵢ`, `Bᵢ`, `B`, khoảng cách bằng nhau và `pull` rút hết frontier. Ghi các khác biệt với Thuật toán 3 và Lemma 3.3 để Người 3 đối chiếu.
- [x] Dùng bộ đếm profiling xác nhận FindPivots và pha relax của BMSSP chuyển sang song song tại **2.048 cạnh hoạt động**. Đã kiểm tra **2.047, 2.048 và 2.049** cạnh và đối chiếu các khoảng cách với Dijkstra.
- [x] Tạo `tests/run_tsan.sh` với `-fsanitize=thread -g -O1` và chạy trong Ubuntu WSL2 x86-64. CTest TSan đạt **3/3** với Clang 18, `libomp`, Archer và `ignore_noninstrumented_modules=1`. Đã lưu cả lượt đạt và lượt cảnh báo runtime để đối chiếu.

**Kết quả mục 2** · [Báo cáo và lệnh chạy lại](bmssp_parallel/person4_results/mutex_verification.md) · [Log Release 7/7](bmssp_parallel/person4_results/ctest_mutex_release.txt) · [Log TSan 3/3](bmssp_parallel/person4_results/tsan_mutex.txt)

## 3. Kiểm thử ConcurrentFrontierD trên bản tích hợp

- [x] Rà soát và chạy lại `tests/test_d_stress.cpp`. Bổ sung ca C++ qua `IFrontierD` cho hai chế độ cleanup và siết kiểm tra lô/cận trong stress test.
- [x] Ca `M=2` rút đủ và đúng thứ tự, bỏ mục lỗi thời, trả cận `4 → 100`. Cleanup tránh cả 2 lần `SPLIT`. Trên đồ thị dày của Người 2, số lần `SPLIT` giảm từ **666** xuống **313** (**53,00%**).
- [x] Stress test Release hoàn thành **32 luồng, 10.000.000 lời gọi frontier** trong **1.864,06 ms**, seed `0x9e3779b97f4a7c15`, timeout CTest **180 giây**, mã thoát `0`.
- [x] Đối chứng `FrontierType::Concurrent` đạt **72/72** lượt. CTest Release đạt **8/8**. ThreadSanitizer trên Ubuntu WSL2 với Clang 18.1.3 và Archer đạt **2/2 Concurrent**, **3/3 Mutex**, 0 báo cáo data race trong cấu hình đã ghi. Phần stress ngẫu nhiên trong lượt TSan dùng 8 luồng và 4.096 lời gọi.

**Kết quả mục 3** · [Báo cáo và lệnh chạy lại](bmssp_parallel/person4_results/concurrent_verification.md) · [Log Release Concurrent](bmssp_parallel/person4_results/ctest_concurrent_release.txt) · [Log TSan Concurrent](bmssp_parallel/person4_results/tsan_concurrent.txt) · [Log TSan Mutex](bmssp_parallel/person4_results/tsan_mutex.txt)

## 4. Đo và tinh chỉnh hiệu năng bản tích hợp

- [x] Tạo `benchmarks/bench_synthetic.cpp` cho Erdős–Rényi và scale-free. Đã đối chiếu fingerprint, nguồn và seed giữa các cấu hình ở **1, 5 và 10 triệu đỉnh**.
- [x] Tạo `benchmarks/run_scaling.py` và đo Release ở **1, 2, 4, 8, 16 và 32 luồng** trên đồ thị một triệu đỉnh với `OMP_PROC_BIND=close` và `OMP_PLACES=cores`. Đã lưu từng mẫu, trung vị, cấu hình build, topology và các thời gian biến đổi, solver, hoàn nguyên, tổng.
- [ ] Xác nhận chính thức với nhóm mốc so sánh cho mục tiêu **≥ 3×**. Báo cáo đã tính speedup `T(1) / T(p)` và hiệu suất `speedup(p) / p` theo mốc Concurrent BMSSP 1 luồng trên cùng cấu hình. Mức tốt nhất đo được là **1,47×**.
- [ ] Thu số liệu cache miss L1/L3 phần cứng. Đã tạo `benchmarks/profile_perf.sh` và chạy riêng với `perf stat`. WSL trả `<not supported>` cho các sự kiện cache. Đã lưu số lần chuyển ngữ cảnh và số liệu chờ khóa D1 từ bản profiling riêng.
- [x] So sánh **1.024, 2.048 và 4.096 cạnh** với cap `M` tự động, 256 và 1.024. Đã chạy lại với hai cấu hình tốt nhất trong lượt quét và đối chiếu đúng với Dijkstra. Cap `M=16` vượt timeout trong lượt thăm dò. Kết quả chạy lại chưa cho thấy lợi ích ổn định để đổi cấu hình mặc định chung.

**Kết quả mục 4** · [Báo cáo và cách chạy lại](bmssp_parallel/person4_results/scaling_report.md) · [Biểu đồ](bmssp_parallel/person4_results/scaling_1m.png) · [Mẫu thô một triệu đỉnh](bmssp_parallel/person4_results/scaling_1m_final.json)

## 5. Kiểm tra hồ sơ bàn giao

- [x] Hồ sơ có lệnh build, lệnh chạy từng bộ và log CTest Release đạt **8/8**, gồm atomic, gadget và đối chứng Dijkstra.
- [x] Có log bàn giao stress **32 luồng, 10 triệu lời gọi**, mã thoát `0`. Hai lượt nghiệm thu TSan đạt **3/3 Mutex** và **2/2 Concurrent**, **0 báo cáo data race** với Clang 18.1.3, `libomp`, Archer và `ignore_noninstrumented_modules=1`. Lượt stress TSan dùng 8 luồng, 4.096 lời gọi.
- [ ] Hồ sơ đã có dữ liệu thô, biểu đồ speedup và hiệu suất. Speedup tốt nhất **1,47×** dưới mục tiêu **≥ 3×**, với scale-free một triệu đỉnh và 8 luồng trong Ubuntu WSL trên máy 16 lõi vật lý. Còn thiếu số đo L1/L3 thực tế. Log `perf` ghi lỗi không hỗ trợ của WSL và danh sách WPR ghi các nguồn đếm Windows khả dụng để đối chiếu.

**Hồ sơ bàn giao** · [Tổng hợp và lệnh tái lập](bmssp_parallel/person4_results/handoff_person4.md) · [Log Release](bmssp_parallel/person4_results/handoff_release.txt) · [Log TSan](bmssp_parallel/person4_results/handoff_tsan.txt)
