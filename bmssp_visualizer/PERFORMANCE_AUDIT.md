# Kiểm tra vì sao BMSSP chậm hơn Dijkstra

Ngày 14/11/2026. Phạm vi: đọc mã hiện tại, đối chiếu bài báo, kiểm tra phạm vi timer và đếm thao tác heap; không chạy benchmark thời gian mới.

## Những gì đã xác nhận

- Bài báo gốc chứng minh cận trên tiệm cận O(m log^(2/3)n) trong mô hình comparison-addition. Không suy ra số giây của mọi bản triển khai trên mọi họ đồ thị thấp hơn Dijkstra.
- Tỷ số riêng của hai thừa số log là (log2 n)^(1/3): 1,880 ở n100; 2,152 ở n1000; 2,711 ở n1.000.000. Tăng n1000 lần từ1000 lên1triệu chỉ tăng tỷ số này khoảng1,26 lần. Đây là so thừa số log, không phải dự đoán thời gian hoặc điểm vượt thực tế.
- Mã upstream có các phần tương ứng FindPivots, BaseCase với cutoff k+1, BMSSP đệ quy và batchPQ. Điều đó chưa phải kiểm chứng đầy đủ mọi bất biến và cận tệ nhất của cấu trúc dữ liệu/thuật toán chọn median. Không có chứng cứ từ lần rà này để kết luận triển khai sai độ phức tạp, cũng không có chứng minh rằng nó hoàn toàn đúng.
- Đã xác nhận lỗi số thực ở upstream: sanitize làm tròn khóa tới1e-10. Bản demo đã bỏ bước này. Hai graph100/1000 hiện có dùng trọng số nguyên1–20 nên không có bằng chứng lỗi làm tròn gây ra chênh lệch thời gian trên chúng.
- Hai phép đo native có phạm vi khác: BMSSP demo tính prepare_graph + execute + dựng lại cây predecessor theo cạnh tight; khởi tạo solver/addEdge nằm trước timer. Dijkstra tính tạo adjacency + chạy heap, ghi predecessor ngay khi relax. Baseline upstream riêng còn có phạm vi khác (constructor/addEdge/prepare/execute/destructor). Các số đã công bố mô tả đúng chương trình trong phạm vi timer, chưa cô lập thuật toán thuần để quy nguyên nhân chậm.
- Không có render hoặc ACK wait trong các mẫu fast đã công bố; không thể quy toàn bộ chênh lệch cho giao diện.
- Đếm bằng triển khai heap độc lập cùng thứ tự cạnh và quy tắc relax strict: heap lớn nhất23 entry cho graph100,88 entry cho graph1000. Đây là bằng chứng frontier nhỏ trên các mẫu này; chưa phải định lượng bao nhiêu thời gian chênh lệch do frontier.
- Các tham số BMSSP thực tế: graph100 k1/t3/3 mức; graph1000 k2/t4/3 mức. Chưa có cơ sở suy ngưỡng n thắng từ hai điểm.

## Nguồn

- Bài báo gốc: ../2504.17033v2.pdf, định lý1.1 và Lemma3.12.
- [Nghiên cứu triển khai của nhóm tác giả repo, v3](https://arxiv.org/html/2511.03007v3): báo cáo Dijkstra nhanh hơn khoảng3–4 lần trên các trường hợp đã thử và phân tích hằng số lớn. Đây là kết quả của tác giả trên cấu hình/họ đồ thị của họ, không phải chứng nhận mã của chúng ta.
- Mã: native/runner.cpp, native/dijkstra.cpp, native/baseline.cpp, native/bmssp-traced.hpp và ../bmssp_comparison/repos/cpp/include/bmssp-worst-case.hpp.
- Số đếm/độ bậc/tham số của hai input: exports/diagnostic-audit.json.

## Điều cần làm để kết luận sâu hơn

Chuẩn hóa cùng phạm vi timer (solver-only hoặc end-to-end được định nghĩa giống nhau), tách dựng cây/khởi tạo, profile từng pha, kiểm nhiều seed và nhiều họ đồ thị. Muốn xác nhận cận lý thuyết của repo cần rà chi tiết bất biến và chi phí khấu hao, không chỉ đối chiếu kết quả với Dijkstra hoặc quan sát chạy chậm.
