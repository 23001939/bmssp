# Kiểm thử BMSSP với MutexFrontierD

Người 4 kiểm thử bản `b3ffe2f` đã tích hợp vào `feat/person4-qa` qua merge commit `8eaf63d`, với các thay đổi kiểm thử trong mục 2. Đối chứng khoảng cách dùng Dijkstra trên cùng đồ thị và nguồn. Bellman–Ford kiểm tra chéo tập dữ liệu sinh ngẫu nhiên.

## Kết quả

- Windows, GCC 14.2.0, CMake Release có bật profiling. CTest đạt **7/7**. `verify_correctness` đạt **72 lượt BMSSP Mutex** từ 18 ca ở 1, 2, 4 và 8 luồng yêu cầu. Test kiểm tra trạng thái tới được của mọi đỉnh, sai số `< 1e-9`, tính hợp lệ của đường đi qua đỉnh cha và đáp án chính xác cho near-tie. [Log Release](ctest_mutex_release.txt)
- Có 8 ca định tuyến cạnh tại các cận `B′ᵢ`, `Bᵢ`, `B` và trường hợp khoảng cách bằng nhau. `MutexFrontierD` trả cận `B` khi rút lô cuối và khi `pull` trên frontier rỗng. [Mã kiểm thử](../tests/verify_correctness.cpp)
- Bộ đếm profiling xác nhận cả FindPivots và pha relax của BMSSP chạy tuần tự với **2.047** cạnh, chạy song song từ **2.048** cạnh với cấu hình 4 luồng. Khoảng cách ở cả ba mốc khớp Dijkstra. Đồ thị dày với seed `20261122` có 4.096 cạnh hoạt động trong một bước FindPivots song song, với nhiều cạnh cùng đích. [Log Release](ctest_mutex_release.txt)
- Ubuntu WSL2 x86-64, Clang 18.1.3, `libomp` và Archer. CTest đạt **3/3**, gồm bộ Mutex, atomic và gadget. Log có **0 báo cáo data race** khi dùng `ignore_noninstrumented_modules=1`. Lượt chẩn đoán chưa bật tùy chọn này báo một cảnh báo ở `pthread_mutex_lock` trong thư viện `libomp` không được instrument. Các thao tác `cmpxchg16b` viết bằng inline assembly được kiểm tra tính nhất quán bằng stress test atomic. [Log TSan với Archer](tsan_mutex.txt) · [Log cảnh báo runtime](tsan_mutex_runtime_warning.txt)

## Điểm cần Người 3 đối chiếu

- Khi một ứng viên cải thiện nghiêm ngặt khoảng cách và nằm dưới `B′ᵢ`, bản tích hợp vẫn đưa vào `K`. Thuật toán 3 mô tả `K` trong khoảng `[B′ᵢ, Bᵢ)`. Đây là khác biệt của bản triển khai đã bàn giao. Các ca đối chứng trong lượt này đều đạt. [Định tuyến cạnh](../src/algorithm/edge_relaxation.hpp) · [Thuật toán 3](https://arxiv.org/html/2504.17033v2)
- Với nhiều đỉnh có cùng khoảng cách, mock có thể trả một lô đủ `M` và cận `Bᵢ` bằng khoảng cách của đỉnh vừa rút. Lõi BMSSP có nhánh xử lý trường hợp đồng hạng này. Cận tách nghiêm ngặt trong Lemma 3.3 dựa trên thứ tự đầy đủ của các khóa. [Mock frontier](../include/core/frontier_interface.hpp) · [Lõi BMSSP](../src/algorithm/bmssp.cpp) · [Lemma 3.3](https://arxiv.org/html/2504.17033v2)

## Chạy lại

Chạy từ thư mục gốc repo bằng Ninja và GCC để kiểm tra Release có bộ đếm.

```powershell
cmake -S bmssp_parallel -B bmssp_parallel/build/person4-mutex -G Ninja -DCMAKE_BUILD_TYPE=Release -DBMSSP_ENABLE_PROFILING=ON
cmake --build bmssp_parallel/build/person4-mutex
ctest --test-dir bmssp_parallel/build/person4-mutex --no-tests=error --output-on-failure
```

Chạy từ thư mục gốc repo trong Ubuntu WSL có Clang 18, Ninja, runtime TSan và Archer.

```bash
CXX=clang++ CMAKE_GENERATOR=Ninja \
  OMP_TOOL_LIBRARIES=/usr/lib/llvm-18/lib/libarcher.so \
  TSAN_OPTIONS=halt_on_error=1:exitcode=66:external_symbolizer_path=/usr/bin/llvm-symbolizer-18:ignore_noninstrumented_modules=1 \
  bash bmssp_parallel/tests/run_tsan.sh
```
