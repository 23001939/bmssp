# Các bước tiếp theo của Người 4 trong dự án BMSSP

Bạn phụ trách kiểm thử tính đúng, an toàn đa luồng và hiệu năng của `bmssp_parallel/`. Trên nhánh `feat/person4-qa`, **mục 1 đã hoàn tất**. Việc tiếp theo là lấy nhánh hoặc commit bàn giao của Người 3 cùng cách gọi API BMSSP song song với `MutexFrontierD`, rồi nối bộ đối chứng đã chuẩn bị vào solver.

[Phân công Người 4 và tiêu chí nghiệm thu](team_allocation_and_technical_workflow.md#34-người-4-kỹ-sư-kiểm-thử-threadsanitizer--tinh-chỉnh-hiệu-năng-giai-đoạn-3---làm-cuối) · [Thiết kế BMSSP song song](bmssp_parallelization_design_document.md) · [Bài báo gốc, Thuật toán 1–3 và Lemma 3.3](2504.17033v2.pdf)

## Trạng thái ngày 20/11/2026

| Mốc | Trạng thái |
| --- | --- |
| 1. Kiểm thử hạ tầng | Hoàn tất. Log và lệnh chạy lại ở mục 1. |
| 2. Kiểm thử BMSSP với `MutexFrontierD` | Chờ Người 3 bàn giao lõi và cách gọi API. Có thể chuẩn bị môi trường ThreadSanitizer trong lúc chờ. |
| 3. Kiểm thử `ConcurrentFrontierD` | Chờ Người 2 bàn giao frontier đồng thời để chạy stress test và kiểm tra lại bản tích hợp. |
| 4. Benchmark và profiling | Chờ bản tích hợp đạt kiểm thử tính đúng và đồng thời. |

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

## 2. Khi Người 3 bàn giao lõi chạy với MutexFrontierD

- [ ] Nối `tests/verify_correctness.cpp` với API solver được bàn giao. Dùng cùng đồ thị gốc và nguồn cho hai solver. Kiểm tra trùng trạng thái tới được của mọi đỉnh và sai số tuyệt đối lớn nhất **nhỏ hơn `1e-9`** trên toàn bộ khoảng cách hữu hạn. Khi lỗi, in đỉnh, đáp án đối chứng, giá trị thực tế, seed và số luồng.
- [ ] Thêm ca kiểm tra ứng viên đúng tại các cận `B′ᵢ`, `Bᵢ`, `B`, ứng viên có khoảng cách bằng giá trị hiện tại và `pull` rút hết frontier. Đối chiếu với Thuật toán 3 và Lemma 3.3. Báo lại nhóm nếu lõi hoặc mock cho kết quả khác hợp đồng đã thống nhất.
- [ ] Cùng Người 3 xác nhận chế độ tuần tự và song song ở hai phía ngưỡng mặc định **2.048 cạnh hoạt động**. Nếu có cách quan sát nhánh thực thi, kiểm tra cả hành vi chọn nhánh và kết quả khoảng cách.
- [ ] Tạo `tests/run_tsan.sh` với cờ biên dịch và liên kết `-fsanitize=thread -g -O1`. Dùng môi trường có hỗ trợ TSan, chẳng hạn Linux x86-64, rồi chạy bộ test đa luồng, gồm đồ thị dày. Lưu log riêng để đối chiếu lại sau khi thay frontier. [Tài liệu ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html)

## 3. Khi Người 2 bàn giao ConcurrentFrontierD

- [ ] Viết `tests/test_d_stress.cpp`. Dùng nhiều luồng gọi `insert`. Xen kẽ các pha `batch_prepend` và `pull` với pha chèn, theo hợp đồng [`IFrontierD`](bmssp_parallel/include/core/frontier_interface.hpp). `batch_prepend` do luồng chính gọi sau điểm đồng bộ.
- [ ] Dùng ca có đáp án biết trước để kiểm tra mục hợp lệ không bị mất, mục lỗi thời bị lọc, dãy `Bᵢ` không giảm qua các lần `pull` trong luồng xử lý hợp lệ, và cận cuối đúng theo hợp đồng. Dùng block nhỏ và nhiều lần giảm khoảng cách để ép `SPLIT` và kiểm tra Eager Cleanup. Nếu module cung cấp bộ đếm, ghi số lần split có và không có cleanup để đối chiếu mục tiêu giảm ít nhất **50%** trên đồ thị dày của Người 2.
- [ ] Chạy mốc **32 luồng, 10 triệu thao tác**. Ghi seed, thời lượng, mã thoát và kết quả. Đặt timeout để ghi nhận lượt bị treo. Mốc đạt khi test hoàn tất và mọi kiểm tra bất biến đều qua.
- [ ] Tích hợp frontier thật theo cờ `USE_CONCURRENT_D` khi nhóm cung cấp. Chạy lại toàn bộ đối chứng và TSan với frontier thật. Lưu log của hai cấu hình để truy lỗi theo lần bàn giao.

## 4. Đo và tinh chỉnh hiệu năng bản tích hợp

- [ ] Tạo `benchmarks/bench_synthetic.cpp` cho đồ thị ngẫu nhiên Erdős–Rényi và scale-free. Giữ cố định đồ thị, nguồn và seed khi đổi số luồng. Mốc nghiệm thu dùng đồ thị từ **1 triệu đỉnh**. Dải khảo sát trong phân công là **1 đến 10 triệu đỉnh**.
- [ ] Tạo `benchmarks/run_scaling.py` để chạy build Release ở **1, 2, 4, 8, 16 và 32 luồng** với `OMP_PROC_BIND=close` và `OMP_PLACES=cores`. Ghi số luồng thực tế, CPU và số lõi vật lý, commit, compiler flags, `n`, `m`, nguồn, seed, từng mẫu đo và trung vị. Ghi riêng thời gian biến đổi đồ thị, thời gian solver và thời gian tổng.
- [ ] Xác nhận với nhóm mốc so sánh dùng cho mục tiêu **≥ 3×**. Với biểu đồ scaling, dùng bản song song chạy 1 luồng làm mốc. Tính `speedup(p) = T(1) / T(p)` và `hiệu suất(p) = speedup(p) / p`. Nếu báo thêm so sánh với `bmssp_seq` hoặc Dijkstra, ghi rõ solver đối chuẩn và phạm vi đo ở từng bảng.
- [ ] Thêm `benchmarks/profile_vtune.sh` hoặc dùng `perf stat` / `perf record` trên môi trường phù hợp. Thu số liệu cache miss L1/L3 và thời gian chờ hoặc tranh chấp khóa theo bộ đếm khả dụng. Chạy profiling riêng với lượt đo thời gian.
- [ ] So sánh cutoff **1.024, 2.048 và 4.096 cạnh** và các giá trị `M` mà bản tích hợp hỗ trợ. Đo trên cùng đồ thị và phần cứng. Chạy lại đối chứng với cấu hình được chọn trước khi chốt kết quả.

## 5. Kiểm tra hồ sơ bàn giao

- [ ] Có lệnh build và chạy lại từng bộ test, cùng kết quả kiểm tra atomic, gadget và đối chứng với Dijkstra.
- [ ] Có log stress test 32 luồng, 10 triệu thao tác và log TSan với **0 cảnh báo data race** trên các lượt nghiệm thu.
- [ ] Có dữ liệu thô, biểu đồ speedup, đường cong scaling, hiệu suất song song và báo cáo cache L1/L3. Ghi giá trị đo được so với mục tiêu **≥ 3× trên CPU 8–16 lõi thực với đồ thị từ 1 triệu đỉnh**. Nếu chưa đạt, ghi cấu hình và kết quả thực tế.
