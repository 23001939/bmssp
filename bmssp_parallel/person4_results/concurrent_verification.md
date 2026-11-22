# Kiểm thử ConcurrentFrontierD của Người 4

Kiểm thử bản tích hợp `b3ffe2f` trên nhánh `feat/person4-qa` sau merge commit `8eaf63d`, với các ca C++ bổ sung ở mục 3.

## Kết quả Release

| Kiểm thử | Kết quả |
| --- | --- |
| Ca xác định `M=2` | Cả hai cấu hình cleanup trả đúng các lô `[2,3]`, `[0,1]` với cận `4`, `100`. Mục lỗi thời giữa hai tầng được lọc. Eager Cleanup tránh cả **2 lần `SPLIT`**, từ 2 xuống 0. |
| Đồ thị dày của Người 2 | 2.048 đỉnh, 200.000 cạnh, seed `20261120`, `M=32`, 8 luồng. Hai cấu hình rút đúng mỗi đỉnh một lần. Số lần `SPLIT` giảm từ **666 xuống 313**, tương đương **53,00%** trong lượt ghi log. Kết quả phụ thuộc thứ tự chạy của các luồng. |
| Stress test | **32 luồng, 10 vòng, 10.000.000 lời gọi** trong **1.864,06 ms**. Seed gốc `0x9e3779b97f4a7c15`. Timeout CTest **180 giây**, mã thoát `0`. Đạt **1.769.649/1.769.649** kiểm tra trong cả suite. |
| Đối chứng BMSSP | **72/72 lượt** trên 18 ca ở 1, 2, 4 và 8 luồng, với `FrontierType::Concurrent`. Sai số `< 1e-9` và near-tie khớp chính xác. Cả bộ CTest Release đạt **8/8**. |

10 triệu lời gọi gồm 9.750.000 `insert`, 125.000 `pull` và 125.000 `batch_prepend`. Tổng này bao gồm lần gọi có ứng viên lỗi thời hoặc lô rỗng. Kịch bản ngẫu nhiên cố tình xen kẽ các thao tác giữa nhiều luồng. Ca theo pha và ca xác định `M=2` kiểm tra quy trình chuẩn, trong đó luồng chính gọi `batch_prepend` sau pha chèn. [Log Concurrent Release](ctest_concurrent_release.txt)

## Kết quả ThreadSanitizer

Ubuntu WSL2 x86-64, Clang **18.1.3**, `libomp` và Archer, với `-fsanitize=thread -g -O1` và `ignore_noninstrumented_modules=1`.

- **Concurrent đạt 2/2** với `verify_correctness_concurrent` và `test_d_stress`, 0 báo cáo data race. Phần stress ngẫu nhiên dưới TSan dùng **8 luồng, 4.096 lời gọi**, giữ các kịch bản theo pha, Eager Cleanup và đối chứng. [Log TSan Concurrent](tsan_concurrent.txt)
- **Mutex đạt lại 3/3** với `verify_correctness`, `test_atomic_state` và `test_gadget_transform`, 0 báo cáo data race trong cấu hình trên. [Log TSan Mutex](tsan_mutex.txt)

## Chạy lại

Release trên Windows với GCC, Ninja và CMake.

```powershell
cmake -S bmssp_parallel -B bmssp_parallel/build/person4-mutex -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release -DBMSSP_ENABLE_PROFILING=ON
cmake --build bmssp_parallel/build/person4-mutex
ctest --test-dir bmssp_parallel/build/person4-mutex --no-tests=error --verbose -R '^(test_d_stress|verify_correctness_concurrent)$'
```

ThreadSanitizer trong Ubuntu WSL từ thư mục gốc repo.

```bash
CXX=clang++ CMAKE_GENERATOR=Ninja \
  OMP_TOOL_LIBRARIES=/usr/lib/llvm-18/lib/libarcher.so \
  TSAN_OPTIONS=halt_on_error=1:exitcode=66:external_symbolizer_path=/usr/bin/llvm-symbolizer-18:ignore_noninstrumented_modules=1 \
  bash bmssp_parallel/tests/run_tsan.sh '' '' concurrent
```
