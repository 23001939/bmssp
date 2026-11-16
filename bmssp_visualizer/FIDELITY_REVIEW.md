# Đối chiếu triển khai với bài báo BMSSP

Ngày 14/11/2026. Phạm vi: đọc PDF `2504.17033v2.pdf`, mã đang dùng và kiểm tra một phản ví dụ đầu ra; không chạy benchmark mới, không sửa solver. Đây là review kỹ thuật, không phải chứng minh hình thức. Phạm vi review hoàn thành; các yêu cầu sửa bên dưới chưa triển khai.

## Kết luận

Ứng dụng có lõi BMSSP thật, không phải Dijkstra đổi tên. Tuy nhiên chưa đủ căn cứ gọi bản hiện tại là triển khai đã kiểm chứng đầy đủ định lý O(m log^(2/3) n), hoặc bộ demo chứng minh BMSSP nhanh hơn Dijkstra. Có lỗi đầu ra đường đi chính xác được tái hiện, điều kiện áp dụng định lý chưa được bảo đảm với dữ liệu bất kỳ, và phạm vi đo thời gian không đồng nhất.

## Mapping và giới hạn

| PDF | Code native/bmssp-traced.hpp | Nhận xét |
|---|---|---|
| Trang 3: bậc vào/ra hằng số, phép chuyển đổi bằng chu trình cạnh 0 | prepare_graph dòng 337; runner.cpp:49 luôn truyền false | Không áp dụng nguyên bảo đảm của bài cho đồ thị bậc tùy ý. Các đồ thị lưới 100/1000 đang thử có bậc nhỏ, nên thiếu chuyển đổi không tự giải thích việc chậm ở hai mẫu này. |
| Trang 4: k, t, mức đệ quy; thứ tự toàn phần của đường đi | Dòng 416–433; uniqueDistT 508–530 | Có tham số và tuple distance/hop-count/vertex/predecessor phù hợp ý tưởng so sánh O(1). Bản demo bỏ làm tròn của upstream. Chưa xác minh mọi bất biến cập nhật tie. Double không phải số thực chính xác của mô hình lý thuyết. |
| Algorithm 1, trang 6 | findPivots 541–586 | Có k vòng, ngưỡng k|S| và pivot. Wi trong PDF là tập; nw_active là vector, push tại 563, chỉ khử trùng vis tại 568–570, không khử trùng active. Vì vậy chưa thể lấy trực tiếp lập luận mỗi vòng O(|W|). Nhãn root lan theo relax rồi đếm thay cho dựng rừng F cuối cùng: cần chứng minh tương đương. Chưa xác nhận phản ví dụ sai khoảng cách từ hai khác biệt này. |
| Algorithm 2, trang 9 | baseCase 589–619 | Có <=, bound <B và cutoff k+1. Lazy heap thay decrease-key; cần xác minh uniqueness của đỉnh được trích và giới hạn số bản ghi. Khác cách cài đặt không tự đồng nghĩa sai. |
| Algorithm 3, trang 10 | bmsspRec 623–699 | Có FindPivots, Pull, đệ quy, quota, hai khoảng nửa mở, BatchPrepend và hợp W. Có D.erase bổ sung. Cần kiểm tra các bất biến tập trả về đầy đủ, rời nhau, giới hạn B và điều kiện đầu vào từng lời gọi. |
| Lemma 3.3, trang 6–8 | batchPQ 46–303 | Có D0/D1, cây upper bounds, median selection, split và prepend. Chưa kiểm chứng đầy đủ bất biến, amortization và giới hạn worst-case của mọi thao tác chỉ từ cấu trúc mã/chú thích. |

Rủi ro mã chưa có phản ví dụ: iterator `it_min` (61) mặc định, truyền vào lower_bound (106/248), comparator (53–57) dereference iterator nếu bound bằng nhau. Cần xác minh nhánh ấy có thể tới với iterator này hay không trước khi kết luận undefined behavior trong một lần chạy. Không coi đây là lỗi đã tái hiện hoặc nguyên nhân chậm đã xác nhận.

## Lỗi đầu ra đã tái hiện

Đồ thị 3 đỉnh, nguồn 0:

```text
3 3
0 1 0.00000000004
0 2 0
2 1 0
```

Runner hiện tại trả distances=[0,0,0], predecessors=[null,0,0]. Khoảng cách đúng, nhưng đường đi xuất tới 1 là 0→1 giá 4e-11, trong khi đường đi ngắn nhất chính xác là 0→2→1 giá 0. `runner.cpp:61–69` bỏ rawpred rồi dựng lại cây bằng BFS, chấp nhận cạnh với dung sai 1e-9. `data.py:204,243` kiểm tra cây bằng cùng dung sai, vì thế `verification.ok=true` vẫn xuất hiện. exact_mismatches=0 chỉ kiểm tra mảng khoảng cách, không chứng nhận cây chính xác.

Bằng chứng: `exports/fidelity-near-tie.json`. Đây là lỗi so với yêu cầu xuất đường đi ngắn nhất chính xác, không phải chứng cứ lõi BMSSP trả sai khoảng cách trên ca này. Upstream chưa vá có lỗi riêng làm tròn khóa 1e-10, đã được regression trước đây xác nhận; không dùng baseline đó làm oracle số thực chính xác.

## Dijkstra và thời gian

`native/dijkstra.cpp:55–70` là binary min-heap thật, bỏ bản ghi cũ, relax khi cải thiện nghiêm ngặt, xử lý hết heap, không dừng ở đích. Không thấy dấu hiệu shortcut làm sai phép so sánh nhiệm vụ SSSP trong vòng lặp này.

Vì dùng lazy insertion thay decrease-key, cận trực tiếp an toàn là O(n + m log(m+1)) thời gian, O(n+m) bộ nhớ với đa đồ thị; trên đồ thị đơn hoặc họ thưa m=O(n) đang thử, có thể viết O((n+m)log n). Không phải bản O(n²) hoặc Fibonacci heap O(m+n log n).

Big-O là cận trên tiệm cận, không phải thời gian thực tế bằng biểu thức đó, không phải cận dưới cho Dijkstra trên mọi họ đầu vào. BMSSP có cận tốt hơn trên đồ thị thưa không kéo theo nhanh hơn mọi đồ thị với n đủ lớn. Cần n, m, topology, nguồn, trọng số, hằng số triển khai và bộ nhớ/cache. Một đường thẳng có heap Dijkstra gần như kích thước 1, cho thấy không thể lấy n log n làm lượng việc bắt buộc của mỗi đầu vào.

Các trung vị cũ (ms): n100: Dijkstra .029, BMSSP .197; n1000: .210 và .764. Dijkstra tăng khoảng 7.24 lần, BMSSP 3.88 lần; tỷ số BM/D giảm 6.79 xuống 3.64. Hai điểm chưa chứng minh tốc độ tăng tiệm cận hoặc ngưỡng vượt.

Đo hiện tại: BMSSP runner.cpp:47–72 tính prepare+execute+dựng cây, không tính constructor/addEdge trước timer; Dijkstra.cpp:49–71 tính xây adjacency+khởi tạo+heap, ghi predecessor ngay khi relax. Baseline còn có phạm vi riêng. Bởi vậy không quy toàn bộ chênh lệch cho lõi thuật toán. Chế độ plain không gồm render/chờ điều khiển; không thể quy việc chậm đơn thuần cho giao diện.

Nghiên cứu của tác giả repo, https://arxiv.org/html/2511.03007v3, cũng báo Dijkstra nhanh hơn 3–4 lần trên các bộ họ thử. Đây là bối cảnh thực nghiệm độc lập, không chứng nhận tính đúng của bản demo đã chỉnh sửa.

## Để đáp ứng mục tiêu repo nghiên cứu/demo chuẩn

1. Sửa đường đi xuất chính xác; giữ và kiểm tra raw predecessor, tách kiểm tra chính xác với kiểm tra gần đúng, không gọi tolerance-pass là chứng minh.
2. Bảo đảm giả thiết bậc hằng hoặc chuyển đổi đúng kèm ánh xạ ID; công khai miền đầu vào và giới hạn số học.
3. Hoàn tất chứng minh tương đương các chỗ thay tập bằng vector, cập nhật root, lazy heap, batchPQ; thêm assertion bất biến trong chế độ audit. Test qua không thay thế chứng minh amortized complexity.
4. Test đối chiếu độc lập (ví dụ Bellman–Ford với số nguyên chính xác), nhiều seed và ca cạnh 0, tie, chu trình, cạnh song song, bậc lớn, không tới được; test riêng hợp đồng Lemma 3.3 và từng thủ tục.
5. Chuẩn hóa cùng phạm vi thời gian, tách core/setup/path-output/trace; bổ sung đếm relax, heap, pivot, Pull/Insert/BatchPrepend. Đo nhiều kích thước và họ đồ thị, công bố kết quả kể cả BMSSP chậm hơn.

Nên định vị sản phẩm là “tái hiện thuật toán, kiểm thử và khảo sát thực nghiệm”, không hứa “chạy demo chứng minh nhanh hơn Dijkstra”. Giữ repo C++ hiện có làm nền là hợp lý, nhưng chưa gọi phiên bản đang dùng là bản đã được chứng nhận đúng toàn bộ bài báo.
