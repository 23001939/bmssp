# Hồ sơ bàn giao Người 4

Xác minh ngày **22/11/2026** trên nhánh `feat/person4-qa`, từ merge commit `8eaf63d` và các thay đổi QA, benchmark trong thư mục làm việc. Lượt chạy lại xác nhận **8/8 test Release**, stress **32 luồng với 10 triệu lời gọi**, và **5/5 test trong hai cấu hình TSan**. Các bộ kiểm thử viết bằng C++20.

## Kiểm thử và nhật ký

| Bộ test | Nội dung và kết quả |
| --- | --- |
| `test_person1` | **153.932/153.932** kiểm tra hạ tầng, gồm atomic, gadget, memory pool, primitive song song và BMSSP tuần tự đối chiếu Dijkstra. |
| `test_atomic_state` | Snapshot 128-bit giữ `dist`, `pred` và `epoch` nhất quán khi thử với 8 và 16 luồng ghi. |
| `test_gadget_transform` | Bậc vào/ra tối đa 2 và khoảng cách sau hoàn nguyên khớp Dijkstra, kể cả nguồn cô lập. |
| `verify_correctness` và `verify_correctness_concurrent` | Mỗi frontier đạt **72/72 lượt** trên 18 ca ở 1, 2, 4 và 8 luồng. Các ca bao phủ cận khoảng cách, đồ thị rời, near-tie và đồ thị dày có nhiều cạnh cùng đích. |
| `verify_parallel_bmssp` và `test_parallel_edge_cases` | Hai bộ kiểm thử tích hợp của nhóm đều đạt trong lượt Release. |
| `test_d_stress` | **32 luồng, 10.000.000 lời gọi** trong **1.697,98 ms**, seed gốc `0x9e3779b97f4a7c15`, timeout **180 giây**, mã thoát `0`. Đạt **1.718.505/1.718.505** kiểm tra trong cả suite. |

Các số trên lấy từ lần chạy bàn giao trong Ubuntu WSL với GCC 13.3.0. Các báo cáo mục 2 và 3 giữ số liệu của lượt chạy tại từng mốc, gồm đối chứng hai frontier, các bộ đếm quanh ngưỡng 2.048 và ca Eager Cleanup. [Log Release bàn giao](handoff_release.txt) · [Báo cáo Mutex](mutex_verification.md) · [Báo cáo Concurrent](concurrent_verification.md)

**ThreadSanitizer** chạy bằng Clang 18.1.3, `libomp` và Archer trên Ubuntu WSL2 x86-64, với `ignore_noninstrumented_modules=1`. Lượt Mutex đạt **3/3** và lượt Concurrent đạt **2/2**, cùng mã thoát `0` và **0 báo cáo data race** trong phạm vi cấu hình này. Phần stress ngẫu nhiên dưới TSan dùng **8 luồng và 4.096 lời gọi**. [Log TSan bàn giao](handoff_tsan.txt) · [Cảnh báo runtime ở lượt chẩn đoán trước đó](tsan_mutex_runtime_warning.txt)

## Hiệu năng và cache

Benchmark chạy trên máy Intel Core i7-14650HX **16 lõi vật lý, 24 CPU logic**, qua Ubuntu WSL2. Ma trận chính đo **1, 2, 4, 8, 16 và 32 luồng** cho mỗi họ đồ thị một triệu đỉnh. Khảo sát bổ sung đo ở 5 và 10 triệu đỉnh. Dữ liệu giữ từng mẫu, trung vị, thông tin build và fingerprint đồ thị. Mốc scaling là thời gian solver của **Concurrent BMSSP một luồng, cùng đồ thị và cùng cấu hình**. [Báo cáo benchmark](scaling_report.md) · [Mẫu thô](scaling_1m_final.json) · [Bảng trung vị](scaling_1m_final.csv) · [Biểu đồ scaling và hiệu suất](scaling_1m.png)

| Kết quả cần đối chiếu | Bằng chứng bàn giao |
| --- | --- |
| Speedup ≥ 3× trên CPU 8–16 lõi thực với đồ thị từ một triệu đỉnh | **Chưa đạt trong môi trường đo.** Mức cao nhất ghi nhận là **1,47×**, hiệu suất **18,3%**, với scale-free 1.000.000 đỉnh và 3.999.990 cạnh, 8 luồng OpenMP, cutoff 2.048, `M` tự động và gadget tắt. Trung vị solver từ **1.121,55 ms** ở 1 luồng xuống **764,67 ms** ở 8 luồng. CPU vật lý là 16 lõi trên Windows. WSL báo topology 12 lõi ảo và 24 vCPU. |
| Dữ liệu benchmark theo kích thước và tham số | [5–10 triệu đỉnh](scaling_large.json), [gadget transform](gadget_1m.json), [quét cutoff và `M`](tuning_1m.json), [kiểm tra lại cấu hình ER](tuned_er_recheck.json), [kiểm tra lại cấu hình scale-free](tuned_scale_free_recheck.json). Tất cả các mẫu hoàn thành đều khớp Dijkstra. |
| Cache miss L1/L3 | **Còn thiếu số đo phần cứng.** WSL trả `<not supported>` cho `L1-dcache-load-misses` và `LLC-load-misses`. Windows WPR liệt kê `LLCMisses` và `LLCReference`, song danh sách nguồn đếm chưa có kết quả đo cho BMSSP. Đã lưu log `perf stat`, số lần chuyển ngữ cảnh và thời gian chờ khóa D1 đo riêng. [Log ER](profile_er_1m.txt) · [Log scale-free](profile_scale-free_1m.txt) · [Danh sách bộ đếm Windows](wpr_pmcsources.txt) · [Tài liệu WPR](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/recording-pmu-events) |

Việc xác nhận chính thức mốc speedup với nhóm vẫn còn mở. Số liệu phân tích dùng mốc 1 luồng đã ghi trong checklist và báo cáo tối ưu của nhóm.

## Lệnh chạy lại

Từ thư mục gốc repo trong Ubuntu WSL có GCC, OpenMP, Ninja và CMake. Lệnh thứ ba chạy đủ tám test. Có thể thay bằng vòng lặp sau để chạy từng bộ và xem chi tiết.

```bash
cmake -S bmssp_parallel -B bmssp_parallel/build/person4-scaling -G Ninja \
  -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DBMSSP_ENABLE_PROFILING=OFF
cmake --build bmssp_parallel/build/person4-scaling --parallel 6
OMP_PROC_BIND=close OMP_PLACES=cores OMP_NUM_THREADS=8 \
  ctest --test-dir bmssp_parallel/build/person4-scaling --no-tests=error --output-on-failure
```

```bash
for test in test_person1 test_atomic_state test_gadget_transform verify_correctness \
  verify_correctness_concurrent verify_parallel_bmssp test_parallel_edge_cases test_d_stress; do
  ctest --test-dir bmssp_parallel/build/person4-scaling --no-tests=error --verbose -R "^${test}$" || break
done
```

Chạy TSan trong Ubuntu WSL có Clang 18, runtime TSan, `libomp` và Archer. Đổi đối số cuối giữa `mutex` và `concurrent` để chạy từng cấu hình.

```bash
export CXX=clang++ CMAKE_GENERATOR=Ninja
export OMP_TOOL_LIBRARIES=/usr/lib/llvm-18/lib/libarcher.so
export TSAN_OPTIONS=halt_on_error=1:exitcode=66:external_symbolizer_path=/usr/bin/llvm-symbolizer-18:ignore_noninstrumented_modules=1
bash bmssp_parallel/tests/run_tsan.sh '' /tmp/bmssp-handoff-mutex.txt mutex
bash bmssp_parallel/tests/run_tsan.sh '' /tmp/bmssp-handoff-concurrent.txt concurrent
```

Các lệnh tái lập benchmark, tùy chọn khảo sát và cách tạo lại biểu đồ nằm trong [báo cáo scaling](scaling_report.md#cách-chạy-lại).
