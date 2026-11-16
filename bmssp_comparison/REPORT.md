**So sánh các repo BMSSP — đo tại máy ngày 13/11/2026**

Kết luận trong phạm vi đã đo: `lcs147/bmssp` là repo BMSSP nhanh nhất trong các bản chạy được. Trên đồ thị ngẫu nhiên có trọng số thực và bậc hằng số, 50.000 đỉnh và 199.996 cạnh, bản EXPECTED mất 63,301 ms và bản WC mất 69,428 ms. Dijkstra C++ trong cùng repo mất 16,188 ms.

Chưa có cơ sở xếp hạng tuyệt đối cả sáu repo: Rust, Go và .NET 10 SDK không có trong môi trường này. Python tham chiếu của logtwothirds đã được chạy riêng; thời gian của nó không đại diện cho lõi Rust.

**Cách đo**

- Mã nguồn upstream giữ nguyên. Chỉ thêm wrapper benchmark bên ngoài các repo.
- C++: g++ 16.2.0, `-std=c++20 -O3 -DNDEBUG`. Python: 3.11.9 (tags/v3.11.9:de54cf5, Apr  2 2024, 10:12:12) [MSC v.1938 64 bit (AMD64)].
- Máy: Windows-10-10.0.26300-SP0; AMD64 Family 25 Model 116 Stepping 1, AuthenticAMD; 12 luồng CPU khả dụng.
- Chạy tuần tự trên cùng dữ liệu, source=0. Không tính đọc file, sinh đồ thị, import module và kiểm tra kết quả.
- C++ dùng trung vị 5 lần sau 1 lần làm nóng; Python trung vị 3 lần sau 1 lần kiểm tra/làm nóng. Không so sánh các chênh lệch nhỏ như một kết luận chắc chắn.
- Thời gian bảng chính gồm khởi tạo solver và prepare_graph nếu có. Mảng kề đầu vào được tạo trước thời gian đo; C++ constructor có copy mảng kề. JSON giữ cả thời gian execute riêng của C++.
- Đồ thị ngẫu nhiên gồm đường backbone bảo đảm tới được mọi đỉnh và ba lớp hoán vị ngẫu nhiên. Bậc vào tối đa 4, bậc ra tối đa 5. Vì đã có bậc hằng số, prepare_graph(false) là chế độ phù hợp; prepare_graph(true) đo thêm để thấy chi phí chuyển đổi.
- Các bản source-to-target được chạy với một sink xa hơn mọi đỉnh khác, buộc chúng xử lý mọi đỉnh cần so sánh. Khoảng cách toàn bộ đỉnh được kiểm tra, không chỉ đỉnh đích.
- Oracle: Dijkstra độc lập bằng Python heapq; sai số rel_tol=abs_tol=1e-9. Đây là kiểm tra thực nghiệm trên hữu hạn đồ thị, không phải chứng minh tổng quát.
- Dữ liệu nhỏ và vừa, chủ yếu đồ thị tổng hợp; kết quả không bảo đảm thứ hạng trên đồ thị thực, máy khác hay mọi kích thước.

**Kết quả đầy đủ, đơn vị mili giây; thấp hơn là nhanh hơn**

| Bản triển khai | counterexample_1500 | real_2000 | real_10000 | integer_10000 | zero_ties_10000 | real_50000 |
|---|---:|---:|---:|---:|---:|---:|
| lcs147: Dijkstra C++ (đối chứng) | 0.066 | 0.326 | 2.856 | 2.672 | 2.248 | 16.188 |
| lcs147: BMSSP WC, không chuyển đổi bậc | 0.643 | 2.050 | 12.003 | 13.824 | 11.614 | 69.428 |
| lcs147: BMSSP EXPECTED, không chuyển đổi bậc | 0.565 | 2.095 | 10.896 | 14.713 | 9.870 | 63.301 |
| lcs147: BMSSP WC, có chuyển đổi bậc | 1.740 | 24.779 | 160.538 | 310.927 | 130.781 | 1149.080 |
| lcs147: BMSSP EXPECTED, có chuyển đổi bậc | 1.380 | 23.430 | 147.992 | 282.808 | 100.019 | 1361.610 |
| Dijkstra Python đối chứng của bộ đo | 0.211 | 1.391 | 19.260 | 27.580 | 18.143 | 276.731 |
| nobelk: bản Python tham chiếu | 0.992 | 4.540 | 42.147 | 73.244 | 38.174 | 337.135 |
| bzantium: Python V1 | 0.627 | 2.817 | 46.258 | 65.424 | 41.548 | 404.474 |
| bzantium: Python V2 | WRONG | WRONG | WRONG | 24.661 | 14.528 | WRONG |
| logtwothirds: Python tham chiếu, KHÔNG phải Rust | 26.410 | 262.796 | 1508.028 | 2685.303 | 1432.608 | SKIP |

**Các phát hiện ảnh hưởng đến việc chọn repo**

- Bzantium V2 sai trên cả ba đồ thị ngẫu nhiên trọng số thực (2.000, 10.000, 50.000 đỉnh) và ca phản ví dụ. Các ca nguyên và zero/ties đã chạy thì khớp; không được xếp nó là solver nhanh nhất cho trọng số thực không âm tổng quát.
- Phản ví dụ API trực tiếp: 0→1 (0,9), 0→2 (0,1), 2→1 (0,1), 1→3 (0,1), 0→3 (2,0). Với Graph(1500) để vượt nhánh fallback n<1000, solve(0,3) trả khoảng cách 1,0 thay vì 0,3. Chỉ bốn đỉnh tham gia; các đỉnh còn lại cô lập. Chi tiết trong audit.json.
- Trên ca real_10000, bzantium V1 gọi một base case bound=∞, xử lý toàn bộ 10.000 đỉnh. Nobelk cũng gọi một base case xử lý cả 10.000 đỉnh, trong khi k=3. Các số đo này không chứng minh hiệu năng thuật toán gốc có cutoff k+1.
- Rust logtwothirds chưa được đo. Theo tài liệu upstream, bmssp-fast thay đổi thuật toán, còn auto chọn Dijkstra. Không gộp các chế độ này với BMSSP gốc khi xếp hạng.
- C# chethandvg chưa chạy vì chỉ có .NET runtime 5.0.17, không có .NET 10 SDK. Go mfreeman451 chưa chạy vì không có Go; source singleton chuyển ngay sang thủ tục bucket/delta-stepping, khác Algorithm 3.

**Nguồn công bố, không gộp vào bảng đo tại máy**

- [logtwothirds BENCHMARKS](https://github.com/primaryaesthetics/logtwothirds/blob/main/BENCHMARKS.md): số liệu Rust do tác giả đo. [VARIANTS](https://github.com/primaryaesthetics/logtwothirds/blob/main/VARIANTS.md) giải thích thay đổi của bmssp-fast.
- [C# benchmark-results](https://github.com/chethandvg/breaking-the-sorting-barrier/blob/main/results/benchmark-results.md): khác máy, khác đồ thị, không thể xếp chung với bảng tại máy.

**Commit mã nguồn đã tải**

| Thư mục | Repo | Commit |
|---|---|---|
| cpp | [lcs147/bmssp](https://github.com/lcs147/bmssp) | [0a8eb82805ba](https://github.com/lcs147/bmssp/tree/0a8eb82805bad7f64dfa1b51e17656c9af4c4681) |
| csharp | [chethandvg/breaking-the-sorting-barrier](https://github.com/chethandvg/breaking-the-sorting-barrier) | [c63a7bc34970](https://github.com/chethandvg/breaking-the-sorting-barrier/tree/c63a7bc3497000f91be7f27d06b4584e77ec50a0) |
| go | [mfreeman451/bmssp-go](https://github.com/mfreeman451/bmssp-go) | [538f16b7166f](https://github.com/mfreeman451/bmssp-go/tree/538f16b7166f52f96dae9ed17d3565a44a1fa2fd) |
| python_reference | [nobelk/single-source-shortest-path](https://github.com/nobelk/single-source-shortest-path) | [a0f0f3e7423b](https://github.com/nobelk/single-source-shortest-path/tree/a0f0f3e7423b37e593a4ffeb1a263e4f135e59d3) |
| python_variants | [bzantium/bmssp-python](https://github.com/bzantium/bmssp-python) | [fa7f2f6d10dd](https://github.com/bzantium/bmssp-python/tree/fa7f2f6d10ddbe86752a2cef2d8ce414bb30c67d) |
| rust_python | [primaryaesthetics/logtwothirds](https://github.com/primaryaesthetics/logtwothirds) | [4bc62c61c3af](https://github.com/primaryaesthetics/logtwothirds/tree/4bc62c61c3af0bab97b7e01bab8352ed43b91e4c) |

**Tái lập**

Từ C:\ttss:

```powershell
g++ -std=c++20 -O3 -DNDEBUG bmssp_comparison/bench_cpp.cpp -o bmssp_comparison/bench_cpp.exe
python -X utf8 bmssp_comparison/run_comparison.py
python -X utf8 bmssp_comparison/audit_variants.py
python -X utf8 bmssp_comparison/write_report.py
```

Kết quả thô: results.json. Lỗi tái lập: audit.json. Đồ thị và khoảng cách C++: data/. Các thời gian có thể dao động theo tải nền và bộ nhớ Windows.
