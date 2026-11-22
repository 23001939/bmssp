# Benchmark scaling BMSSP của Người 4

## Cấu hình đo

- Máy Windows dùng Intel Core i7-14650HX, **16 lõi vật lý, 24 CPU logic**. Ubuntu WSL2 thấy **24 vCPU thuộc 12 lõi ảo**. Kích thước team OpenMP được đo bằng một lượt thăm dò với cùng cấu hình. Mức 32 luồng vượt số vCPU của WSL.
- GCC 13.3.0, C++20, `Release`, OpenMP, `-O3 -march=native -mcx16`, profiling tắt cho các lượt đo thời gian. `OMP_PROC_BIND=close`, `OMP_PLACES=cores`, `OMP_DYNAMIC=FALSE`. Tệp JSON lưu commit, dấu vết mã nguồn cục bộ, SHA-256 tệp thực thi và lệnh biên dịch thực tế.
- Hai họ đồ thị có hướng gồm Erdős–Rényi `G(n,p)` với bậc ra kỳ vọng 4, và scale-free sinh bằng gắn kết ưu tiên theo `bậc ra + 1`. Nguồn `0`, seed `20261122`, cạnh trọng số nguyên từ 1 đến 100. Runner kiểm tra fingerprint, số cạnh và số đỉnh tới được khi đổi tham số trên cùng đồ thị.
- Mỗi cấu hình scaling một triệu đỉnh có 1 lượt làm nóng và 3 mẫu. BMSSP được đối chiếu với Dijkstra trên mọi đỉnh ở từng lượt. `solve_ms` gồm khởi tạo solver và trích xuất kết quả. `transform_ms`, `restore_ms` và `total_ms` được lưu riêng. Thời gian sinh đồ thị và chạy Dijkstra đối chứng nằm ngoài thời gian BMSSP.
- Mốc scaling là **Concurrent BMSSP một luồng trên cùng đồ thị và cấu hình**, theo checklist và cột tự tăng tốc `Conc` trong báo cáo nhóm. Speedup bằng trung vị `solve_ms` của một luồng chia cho trung vị tại `p` luồng. Hiệu suất bằng speedup chia cho kích thước team `p`. Việc xác nhận chính thức mốc nghiệm thu với nhóm còn mở. [Quy ước đo của nhóm](../PARALLEL_OPTIMIZATION_REPORT.md)

## Scaling ở một triệu đỉnh

| Họ đồ thị | Số cạnh | Đỉnh tới được | 1 luồng | Nhanh nhất | Speedup | Hiệu suất |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Erdős–Rényi | 4.000.740 | 980.180 | 776,07 ms | 618,15 ms, 4 luồng | 1,26× | 31,4% |
| Scale-free | 3.999.990 | 1.000.000 | 1.121,55 ms | 764,67 ms, 8 luồng | 1,47× | 18,3% |

Cả hai cấu hình mặc định đều **chưa đạt 3×** trong ma trận này. Tệp dữ liệu ghi đủ các mức 1, 2, 4, 8, 16 và 32 luồng. [Mẫu thô và thông tin build](scaling_1m_final.json) · [Trung vị](scaling_1m_final.csv) · [Biểu đồ scaling](scaling_1m.png)

## Khảo sát 5 và 10 triệu đỉnh

Cấu hình mặc định, một mẫu mỗi mức 1 và 8 luồng. Các số đo này bổ sung dải kích thước và dùng ít mẫu hơn ma trận một triệu đỉnh. Mọi lượt đều khớp Dijkstra. [Mẫu thô](scaling_large.json) · [Bảng scaling](scaling_large.csv)

| Họ đồ thị | Số đỉnh | 1 luồng | 8 luồng | Speedup |
| --- | ---: | ---: | ---: | ---: |
| Erdős–Rényi | 5.000.000 | 5.166,25 ms | 3.981,51 ms | 1,30× |
| Erdős–Rényi | 10.000.000 | 9.734,06 ms | 6.814,70 ms | 1,43× |
| Scale-free | 5.000.000 | 5.616,00 ms | 4.080,06 ms | 1,38× |
| Scale-free | 10.000.000 | 10.440,75 ms | 8.222,29 ms | 1,27× |

## Tách thời gian biến đổi đồ thị

Hai lượt riêng có gadget transform ở một triệu đỉnh và 8 luồng. Mỗi họ có một mẫu đã đối chiếu Dijkstra trên đồ thị gốc sau khi hoàn nguyên. Lượt scaling chính chạy trực tiếp trên CSR, nên trường transform và restore ở đó có giá trị 0. [Mẫu gadget](gadget_1m.json) · [Bảng thời gian gadget](gadget_1m.csv)

| Họ đồ thị | Biến đổi | Solver trên đồ thị biến đổi | Hoàn nguyên | Tổng lời gọi |
| --- | ---: | ---: | ---: | ---: |
| Erdős–Rényi | 438,44 ms | 5.002,10 ms | 231,88 ms | 5.673,18 ms |
| Scale-free | 363,03 ms | 5.968,26 ms | 174,16 ms | 6.506,39 ms |

## So sánh cutoff và kích thước block

Đã chạy 18 cấu hình ở 8 luồng trên hai đồ thị một triệu đỉnh. Cutoff gồm 1.024, 2.048 và 4.096 cạnh. Cap `M` gồm tự động, 256 và 1.024. Cap chỉ thu nhỏ kích thước lô của từng tầng, giữ công thức per-level khi kích thước tự động nhỏ hơn cap.

| Họ đồ thị | Cutoff nhanh nhất trong lượt quét | Cap `M` | Trung vị khi quét |
| --- | ---: | ---: | ---: |
| Erdős–Rényi | 4.096 | 256 | 632,87 ms |
| Scale-free | 2.048 | 1.024 | 700,23 ms |

Một lượt thăm dò với Erdős–Rényi, cutoff 1.024, `M=16` vượt timeout 90 giây. Ma trận đầy đủ sau đó dùng các cap lớn hơn. Các cấu hình đang được so sánh theo từng họ đồ thị. Giá trị mặc định của solver tiếp tục lấy cutoff 2.048 và `M` theo từng tầng. [Mẫu thô tinh chỉnh](tuning_1m.json) · [Trung vị tinh chỉnh](tuning_1m.csv) · [Log thăm dò M nhỏ](tuning_low_cap_probe.json)

Lượt kiểm tra lại xác nhận mọi mẫu của hai cấu hình chọn từ ma trận đều khớp Dijkstra. Trung vị ở 1 và 8 luồng lần lượt là **637,21 và 639,24 ms** cho Erdős–Rényi, **677,81 và 848,87 ms** cho scale-free. Speedup 1 → 8 trên cấu hình đã chọn lần lượt khoảng **1,00×** và **0,80×**. Kết quả ở 8 luồng của scale-free biến động so với lượt quét. Các phép đo này chưa cho thấy lợi ích ổn định để đổi mặc định theo một cấu hình chung. [Kiểm tra lại Erdős–Rényi](tuned_er_recheck.json) · [Kiểm tra lại scale-free](tuned_scale_free_recheck.json)

## Profiling riêng ở một triệu đỉnh

`perf stat` 6.8.12 đo được **2.178 lần** chuyển ngữ cảnh ở Erdős–Rényi và **2.255 lần** ở scale-free trên toàn tiến trình, bao gồm sinh đồ thị và Dijkstra. WSL2 trả `<not supported>` cho `L1-dcache-load-misses`, `LLC-load-misses`, `cycles` và `instructions`. Kernel cũng thiếu tracepoint `lock:contention_begin`. Vì vậy chưa có số cache miss L1/L3 thực nghiệm trên máy này. [Log profiling Erdős–Rényi](profile_er_1m.txt) · [Log profiling scale-free](profile_scale-free_1m.txt)

Bản build profiling riêng đo các lần gọi khóa D1 phải chờ từ lần thử đầu tiên. Số liệu gồm khóa block và khóa pool shard. Tổng chờ cộng thời gian của mọi luồng và có thể chịu ảnh hưởng của chính mã đo. Số liệu dưới đây dùng cutoff 2.048, `M` tự động và 8 luồng. Thời gian bảng này phục vụ xác định nút nghẽn theo pha.

| Họ đồ thị | Lần lấy khóa bị tranh chấp | Tổng chờ của các luồng | Thời gian `pull` |
| --- | ---: | ---: | ---: |
| Erdős–Rényi | 1.347 | 97,38 ms | 326,63 ms |
| Scale-free | 3.207 | 99,80 ms | 435,12 ms |

## Cách chạy lại

Từ thư mục gốc repo trong Ubuntu WSL có GCC và Ninja.

```bash
cmake -S bmssp_parallel -B bmssp_parallel/build/person4-scaling -G Ninja \
  -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build bmssp_parallel/build/person4-scaling --parallel 6
ctest --test-dir bmssp_parallel/build/person4-scaling --no-tests=error --output-on-failure
python3 bmssp_parallel/benchmarks/run_scaling.py \
  --binary bmssp_parallel/build/person4-scaling/bench_synthetic \
  --host-physical-cores 16 --output /tmp/bmssp-scaling-repeat.json
```

Runner nhận `--vertices 5000000 10000000`, `--threads`, `--cutoff`, `--block-cap` và `--gadget` để chạy lại các phép đo riêng. Dùng `--plot-from <results.json> --output <chart.png>` với Matplotlib để tạo lại biểu đồ. Chạy profiling trên một thư mục build riêng với `-DBMSSP_ENABLE_PROFILING=ON`. Script `benchmarks/profile_perf.sh` nhận `PERF_BIN` nếu cần trỏ tới tệp thực thi `perf` trực tiếp.
