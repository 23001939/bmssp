# Thẩm định hai mẫu Dijkstra OpenMP và phương án tích hợp

Ngày kiểm tra: **19/11/2026**, múi giờ Asia/Saigon. Bản gốc để đọc, không biên dịch vào thư viện dự án: [snapshots và manifest](references/dijkstra_openmp/README.md).

## Nguồn và giấy phép

1. [actumn/dijkstra-openmp](https://github.com/actumn/dijkstra-openmp), commit [`32c1f44efe0e5fb68d77a5b0e7cef2b0afd7e78c`](https://github.com/actumn/dijkstra-openmp/tree/32c1f44efe0e5fb68d77a5b0e7cef2b0afd7e78c). [Mã gốc](https://github.com/actumn/dijkstra-openmp/blob/32c1f44efe0e5fb68d77a5b0e7cef2b0afd7e78c/dijkstra.cpp), [MIT license](https://github.com/actumn/dijkstra-openmp/blob/32c1f44efe0e5fb68d77a5b0e7cef2b0afd7e78c/LICENSE), Copyright (c) 2019 SunMyeong Lee. Đã giữ nguyên toàn văn license trong `references/dijkstra_openmp/actumn_LICENSE`.
2. [John Burkardt, dijkstra_openmp (FSU)](https://people.sc.fsu.edu/~jburkardt/cpp_src/dijkstra_openmp/dijkstra_openmp.html), [mã C++](https://people.sc.fsu.edu/~jburkardt/cpp_src/dijkstra_openmp/dijkstra_openmp.cpp). Trang HTML ghi MIT và nguồn C++ cũng ghi MIT trong các khối Licensing. Nguồn ghi bản C gốc của Norm Matloff, CS Dept, UC Davis; bản này của John Burkardt. Trang ghi sửa gần nhất 24/02/2020; hàm lõi ghi 02/07/2010. Website không có commit; dùng SHA-256 snapshot trong manifest để nhận diện đúng bản đã đọc. Không tìm được file LICENSE riêng tại thư mục mẫu; giữ nguyên nguồn có thông báo tác giả và tuyên bố MIT, không tự tạo năm bản quyền.

MIT cho phép dùng, sửa và phân phối; điều kiện của bản văn actumn là giữ copyright và permission notice khi phân phối bản sao hoặc phần đáng kể. Tích hợp mới dùng giao diện đồ thị CSR sẵn có và viết riêng thuật toán; các snapshot giữ nguyên để đối chiếu. Không áp giấy phép của hai ví dụ lên toàn repo BMSSP hoặc các dependency có sẵn.

## Cách song song hóa

Dijkstra có phụ thuộc tuần tự giữa các lần chốt đỉnh: phải biết đỉnh chưa chốt có khoảng cách nhỏ nhất toàn cục trước khi relax lượt tiếp. Hai mẫu giữ vòng chốt đỉnh tuần tự về logic, chia việc tìm cực tiểu và cập nhật các đỉnh đích giữa các luồng. Đây là Dijkstra dạng quét, không phải BMSSP hoặc phép song song hóa heap.

**actumn** dùng ma trận kề `int**`. Mỗi lượt mở một `omp parallel`. `omp for` chia các đỉnh để mỗi thread ghi cặp cực tiểu vào ô riêng (`min_array[thread_id]`, `waypoint_array[thread_id]`). Một `single` gộp kết quả, chọn waypoint và đánh dấu selected; `omp for` tiếp theo relax một đích `i` trên đúng một luồng. Hai vòng for có barrier ngầm cuối vòng và single có barrier ngầm cuối khối. Kết thúc parallel cũng đồng bộ. Chi phí mở team ở mọi lượt và chia sẻ các ô cực tiểu kề nhau có thể gây overhead hoặc false sharing.

**Burkardt** giữ **một parallel region** cho toàn solver, chia khoảng đỉnh theo thread ID và team size thực tế. Mỗi thread tìm local minimum. `single` reset cực tiểu chung, `critical` gộp cặp (distance, vertex), barrier đợi mọi ứng viên; `single` chốt một đỉnh; mỗi thread cập nhật phần mảng khoảng cách nó sở hữu rồi barrier trước lượt kế. Với một đỉnh đích do một thread cập nhật, không cần atomic cho mỗi khoảng cách.

## Kiểm tra đồng bộ và correctness

### actumn

- **Không thấy race trên dist/prev/selected trong các pha hiện có**, với đầu vào hợp lệ và team được runtime cấu hình hợp lệ. Barrier ngầm cuối for đảm bảo single đọc đủ local minima; barrier cuối single công bố waypoint và selected; barrier cuối relax ngăn tìm min đọc khoảng cách dở dang. Xóa barrier bằng `nowait` tại đây sẽ phá phụ thuộc. Atomic riêng từng biến không thay thế yêu cầu chọn min của một pha hoàn chỉnh.
- Mảng cấp theo số luồng yêu cầu. Team có thể nhỏ hơn yêu cầu khi dynamic, thread limit hoặc nested parallel; ở mã hiện có các ô min không dùng được reset mỗi lượt nên không tự tạo race hay cực tiểu giả. Dù vậy nên gộp theo team size thực tế, không giả định yêu cầu bằng số chạy thật. `thread_count <= 0` chưa được kiểm tra.
- `MAX=100000` vừa là không-cạnh vừa là khoảng cách khởi tạo: đường ngắn nhất >=100000 không được biểu diễn đúng. Đỉnh không tới được vẫn có thể bị chốt với khoảng cách MAX và khôi phục thành đường chỉ chứa đích; thiếu trạng thái unreachable thật. Đây không phải race.
- `prev[i] = -1` nếu cạnh nguồn có trọng số 0 làm mất cạnh cha hợp lệ; khoảng cách có thể đúng nhưng đường đi sai. Bản parallel cũng không ép `dist[source]=0`, phụ thuộc đường chéo đầu vào phải là 0.
- Cộng `int` không kiểm tra có thể tràn signed integer với đầu vào lớn, sentinel không an toàn. Không từ chối cạnh âm, chỉ số nguồn/đích sai hoặc file không tồn tại.
- Parser dùng `fgetc` vào `char` thay vì `int`, không kiểm tra EOF đúng ở mọi vòng; `curr_field` không được kết thúc NUL trước `atoi`, không kiểm tra độ dài 64 byte; có nguy cơ đọc ngoài vùng, ghi tràn hoặc treo trên CSV thiếu dấu phân cách. Cấp bảng con trỏ bằng `NODE*NODE*sizeof(int)` không đúng công thức `NODE*sizeof(int*)` (đặc biệt NODE=1 trên 64-bit). Buffer path 6 byte không an toàn cho chỉ số lớn.

### Burkardt

- **Race thật trên `nth`**: khai báo `shared(... nth ...)` rồi mọi thread cùng thực hiện `nth = omp_get_num_threads()` và đọc nó để tính khoảng. Các ghi cùng một giá trị vẫn là các truy cập xung đột không được đồng bộ. Sửa bằng `const int nth = omp_get_num_threads()` cục bộ bên trong parallel, hoặc một single ghi và barrier trước mọi đọc. Không bỏ qua lỗi chỉ vì ví dụ thường cho ra đáp án đúng.
- `critical` bảo vệ cặp md/mv nhưng **không có barrier ngầm**; barrier sau critical là cần thiết. Nếu thiếu, có thể chốt ứng viên trước khi luồng chậm cập nhật cực tiểu nhỏ hơn.
- `single` sau cập nhật connected đã có barrier ngầm, vì vậy barrier tường minh ngay kế sau là **dư**, không phải lỗi correctness. Barrier cuối update vẫn cần thiết trước lần quét min tiếp theo.
- Cập nhật mind theo các khoảng đích không giao nhau và chỉ đọc `mind[mv]` của đỉnh đã chốt. Không thấy race trên mind/connected khi giữ đủ các barrier. `cout` giữa nhiều thread có thể xen kẽ nội dung; không dùng log xen kẽ để suy ra thứ tự thuật toán.
- Trọng số không âm, `int` và sentinel INT_MAX: phép cộng hai giá trị hữu hạn vẫn có thể tràn số; mẫu nhỏ không kích hoạt lỗi. Các khoảng cách bằng nhau có thể chọn mv khác nhau theo thứ tự vào critical; vẫn đúng distances nhưng không đảm bảo thứ tự tái lập. Khi hết đỉnh tới được, mv=-1 được kiểm tra trước truy cập nên không truy cập chỉ số -1; vòng còn lại tiếp tục đồng bộ không cần cập nhật.

## Áp dụng cho bmssp_parallel

Dùng `CSRGraph`, `SSSPResult`, `double`, `INF_DIST`, `NO_PRED` và giữ `dijkstra_sequential` heap làm oracle độc lập. Không biến đồ thị thưa thành ma trận O(n²) bộ nhớ. Một parallel region bao toàn bộ các lượt: `omp for` quét đỉnh, mỗi thread ghi local minimum vào slot riêng, barrier trước `single` gộp; `single` gộp theo team size thực tế và chốt đỉnh, barrier ngầm trước `omp for` relax; barrier ngầm cuối relax trước lượt kế. Local minimum dùng cặp (distance, vertex) và chọn ID nhỏ hơn khi hòa. Team size lấy từ runtime, không có nhiều thread ghi biến shared chưa được đồng bộ.

Mỗi pha có quan hệ: **hoàn tất local minima → gộp/chốt một đỉnh → công bố trạng thái chốt → hoàn tất relax → lượt kế**. Phải có barrier hoặc construct kết thúc pha thích hợp tại từng mũi tên. Mọi thread cùng nhìn thấy điều kiện dừng sau đồng bộ và cùng rời vòng; không break riêng một thread trước barrier chung.

Quan trọng với CSR: chia đơn thuần các cạnh ra bằng `omp for` có thể race khi có **cạnh song song cùng đích**. Tích hợp chọn chuẩn hóa từng hàng CSR một lần: sắp xếp theo đích và gộp các cạnh (u,v) trùng nhau bằng trọng số nhỏ nhất. Khi chỉ một u được chốt mỗi lượt, các cạnh ra còn lại có đích phân biệt; `omp for` trên hàng đó bảo đảm một owner ghi cả dist/pred và các owner không ghi chồng nhau. Giữ nguyên đồ thị đầu vào; tiền xử lý nằm trong mỗi lần gọi solver và được tính vào thời gian đo. Không cần 128-bit CAS của BMSSP cho solver này nếu đã bảo đảm một người ghi mỗi đỉnh trong pha relax. Không dùng `vector<bool>` cho cờ chốt nếu các thread cùng ghi bit vì các bit có thể chung một word.

Khởi tạo nguồn bằng 0, còn lại vô cực/NO_PRED; ngừng khi minimum không hữu hạn. **Nguồn ngoài đồ thị trả mảng INF_DIST/NO_PRED**, cùng hợp đồng oracle tuần tự, không ném exception vì nguồn sai. Từ chối CSR malformed, chỉ số đích cạnh sai, cạnh âm/NaN/Infinity và số luồng âm bằng `invalid_argument` trước khi vào parallel. Số luồng 0 chọn `omp_get_max_threads()`. Không dùng epsilon khi chọn min hoặc relax vì thay đổi thứ tự Dijkstra trên near-tie; chỉ có thể dùng dung sai trong phần đánh giá sai số, ghi rõ tiêu chí. `double` không biểu diễn được tổng quá DBL_MAX; cần công bố chính sách overflow trong API và kết quả kiểm thử.

Với dạng quét, công việc tìm min vẫn O(n²), tiền xử lý thêm O(sum_u deg(u) log deg(u)), duyệt cạnh O(m); bộ nhớ O(n+m+p) với p là số luồng. CSR giảm bộ nhớ, không tự biến nó thành solver heap O((n+m)log n). Trên đồ thị đường bộ thưa, barrier mỗi đỉnh và quét min có thể làm chậm hơn baseline heap. Đánh giá trên cùng đồ thị và nguồn, ghi cả setup và solve, warm-up, số mẫu, cấu hình luồng yêu cầu/thực tế. Không lấy kết quả tốc độ từ hai repo mẫu để thay phép đo tại máy.

## Tiêu chí kiểm chứng

Đối chiếu toàn bộ distances với heap Dijkstra hiện có; dùng thêm oracle độc lập cho đồ thị nhỏ nếu có. Kiểm tra riêng đường predecessor có cạnh hợp lệ, đúng tổng, không chu trình; không bắt buộc cùng predecessor khi có nhiều đường bằng nhau nếu chưa thống nhất tie-breaking. Bao gồm mẫu FSU [0,35,15,45,49,41], trọng số 0, chu trình 0, cạnh song song, self-loop, đỉnh cô lập, nguồn khác 0, n=1, hòa khoảng cách/near-tie, số lớn và đầu vào sai. Chạy lặp với 1/2/4 và nhiều luồng hơn số đỉnh, dynamic team/thread limit. Stress pass hữu hạn không phải bằng chứng không có race; ghi rõ nếu chưa chạy detector tương thích OpenMP.

Kết quả thực thi và benchmark do harness của tích hợp mới ghi riêng; tài liệu này chỉ báo cáo thẩm định tĩnh của đúng hai snapshots nêu trên.
