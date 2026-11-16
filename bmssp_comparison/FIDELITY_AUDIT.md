# BẢN MAPPING FIDELITY LỊCH SỬ BMSSP — CHƯA CHỨNG NHẬN

> Post-fix status (2026-11-14): scoped remediation is complete; see REMEDIATION_RESULTS.md at the workspace root for authoritative current status. Production near-tie path, frontier and isolated-node transform fixes passed regression; quick/full manifests passed 47 app/launcher + 10 native tests, and full checked 40 C++/Rust solver-cases. New timing evidence: BENCHMARK_V2_REPORT.md. Historical statements below describe their original snapshot. Full fidelity/theorem certification remains OPEN / NOT CERTIFIED.

*Đối chiếu chi tiết giữa Paper (arXiv:2504.17033v2), Triển khai C++ (`lcs147`) và Triển khai Rust (`logtwothirds`)*

**Ngày thực hiện:** 14/11/2026  
**Môi trường:** Windows x64, MinGW-w64 (`gcc 16.2.0`), Rust 1.100.0 (GNU target).  
**Trạng thái ghi trong bản gốc:** Giai đoạn 4 được đánh dấu hoàn thành; review sau đó hạ trạng thái xuống mapping lịch sử, chưa chứng nhận.

> **Đính chính review độc lập 14/11/2026:** tiêu đề/trạng thái cũ không còn là kết luận nghiệm thu. Đây là ghi chú mapping và kiểm thử lịch sử, không phải audit toàn diện hay chứng minh fidelity/cận. Review mới xác nhận có mã BMSSP thật và test thực nghiệm hữu ích, nhưng còn câu hỏi về `Wi`/active vector, root propagation, uniqueness, hợp đồng thủ tục, worst-case/amortized của BlockDS, cùng lỗi predecessor near-tie trong visualizer. O-3/O-4 vẫn OPEN / NOT CERTIFIED. Các số test chỉ áp dụng đúng tập và cấu hình đã chạy.

Các cụm “khớp”, “đúng” trong bảng mô tả cấu trúc mã hoặc kết quả hữu hạn; không được hiểu là chứng minh tương đương toán học. Tên file/commit và số dòng là snapshot lịch sử, cần đối chiếu checkout trước khi dùng.

---

## 1. BẢNG ÁNH XẠ ĐỐI CHIẾU 3 CỘT (MAPPING TABLE)

Bảng đối chiếu từng dòng mã giữa bài báo gốc (Duan et al., arXiv:2504.17033v2), mã C++ (`bmssp-worst-case.hpp` commit `0a8eb828`) và mã Rust (`src/bmssp.rs` & `src/block_queue.rs` commit `4bc62c...`).

### 1.1. Algorithm 1: FindPivots(B, S) (Paper Trang 6)

| Dòng Paper | Mã giả Paper | C++ `lcs147` (`bmssp-worst-case.hpp`) | Rust `logtwothirds` (`src/bmssp.rs`) | Đánh giá & Ghi chú |
| :---: | :--- | :--- | :--- | :--- |
| **L1** | `function FindPivots(B, S)` | `findPivots(B, S)` (dòng 535) | `fn find_pivots(b, s)` (dòng 322) | Khớp hoàn toàn chữ ký. |
| **L2-3**| $W \leftarrow S; W_0 \leftarrow S$ | `vis.push_back(x); active = S` (dòng 541-546) | `w_set = s; w_order = s; frontier = s` (dòng 326-337) | C++ và Rust đều khởi tạo tập ban đầu từ $S$. |
| **L4** | `for i ← 1 to k do` | `for(int i = 1; i <= k; i++)` (dòng 548) | `for _i in 0..k` (dòng 339) | Đúng $k$ vòng lặp Bellman-Ford. |
| **L5-6**| $W_i \leftarrow \emptyset$; duyệt $(u,v)$ với $u \in W_{i-1}$ | Duyệt qua `active` (dòng 551-552) | Duyệt qua `frontier` (dòng 343-346) | C++ & Rust tối ưu: chỉ relax đỉnh tích cực. |
| **L7-8**| Relax: $\hat{d}[u] + w \le \hat{d}[v] \implies \hat{d}[v] \leftarrow \dots$ | `if(getDist(u, v, w) <= getDist(v)) updateDist` (553-554) | `if self.try_relax(u, v, w)` (dòng 350) | Phép so sánh $\le$ bảo đảm Assumption 2.1. |
| **L9-10**| `if d̂[u] + w < B then W_i ← W_i ∪ {v}` | `if(getDist(v) < B) nw_active.push_back(v)` (555-557) | `if passed && self.key(v) < b` (dòng 352-354) | Chặn $B$ chỉ áp dụng cho lan truyền frontier. |
| **L11** | $W \leftarrow W \cup W_i$ | Đánh dấu `pivot_vis[x] = counter_pivot` (562-567) | `w_set.insert(v); w_order.push(v)` (357-362) | Gom các đỉnh đã vươn tới vào tập $W$. |
| **L12-14**| `if \|W\| > k\|S\| then return (S, W)` | `if(vis.size() > k * S.size()) return {S, vis}` (568-570) | `if w_set.len() > k * s.len() return (s, w_order)` (363-366) | Có nhánh dừng tại vị trí này; vector active chưa chứng minh có cùng giới hạn chi phí với tập $W$. |
| **L15** | Rừng cạnh căng $F \leftarrow \{(u,v) \in E : u,v \in W, \text{tight}\}$ | Lan nhãn `root[v] = root[u]` (dòng 556) | Dựng `children: HashMap` từ `pred` (dòng 372-393) | Hai cơ chế khác nhau; tương đương với cấu trúc rừng ở bổ đề chưa được xác nhận đầy đủ. |
| **L16-17**| $P \leftarrow \{u \in S : |V(T_u)| \ge k\}$; return $(P, W)$ | Đếm `treesz[root[u]]++` $\ge k$ (dòng 576-581) | DFS đếm kích thước cây con $\ge k$ (dòng 396-414) | Trả về tập Pivots $P \subseteq S$ và $W$. |

---

### 1.2. Algorithm 2: BaseCase(B, S) (Paper Trang 9)

| Dòng Paper | Mã giả Paper | C++ `lcs147` (`bmssp-worst-case.hpp`) | Rust `logtwothirds` (`src/bmssp.rs`) | Đánh giá & Ghi chú |
| :---: | :--- | :--- | :--- | :--- |
| **L1** | `function BaseCase(B, S)` | `baseCase(uniqueDistT B, int x)` (dòng 583) | `fn base_case(&mut self, b, s)` (dòng 418) | Nhận singleton $S = \{x\}$. |
| **L2-3**| $U_0 \leftarrow S$; khởi tạo Heap $H \leftarrow \{\langle x, \hat{d}[x]\rangle\}$ | `heap.push(getDist(x))` (dòng 588) | `u0 = vec![x]; heap.push((kx0, x))` (424-443) | Đưa gốc vào heap và khởi tạo $U_0$. |
| **L4** | `while H non-empty and \|U_0\| < k + 1` | `while(!heap.empty() && complete.size() < k+1)` (589) | `while !heap.is_empty() && u0.len() < k+1` (445) | Chặn $k+1$ đỉnh tối đa trong mini-Dijkstra. |
| **L5-6**| $\langle u, \hat{d}[u]\rangle \leftarrow H.\text{ExtractMin}()$; $U_0 \leftarrow U_0 \cup \{u\}$ | `heap.pop(); complete.push_back(u)` (592-596) | `heap.pop(); if in_u0.insert(u) u0.push(u)` (447-454) | Lazy deletion: bỏ qua các pop lỗi thời (stale). |
| **L7-9**| Thư giãn cạnh: $\hat{d}[u]+w \le \hat{d}[v]$ và $< B$ | `if(new_dist <= old_dist && new_dist < B)` (600) | `if cand <= cur && vkey < b` (dòng 480) | Chặn $< B$ ngay trong điều kiện thư giãn. |
| **L10-13**| Cập nhật Heap (Insert / DecreaseKey) | `updateDist(); heap.push(new_dist)` (601-603) | `update; best.insert; heap.push` (481-487) | Cả hai dùng Lazy Heap thay cho Fibonacci/DecreaseKey. |
| **L14-15**| `if \|U_0\| ≤ k then return (B, U_0)` | `if(complete.size() <= k) return {B, complete}` (606) | `if u0.len() <= k return (b, u0)` (492-493) | Heap cạn trước khi đạt $k+1$: trả về toàn bộ $U_0$. |
| **L16-17**| Đạt $k+1$: $B' \leftarrow \max d$; $U \leftarrow U_0 \setminus \{\text{max}\}$ | `nB = getDist(back()); pop_back(); return {nB, complete}` (608-611) | `bp = max(u0); filtered = u0 < bp; return (bp, filtered)` (495-502) | Loại bỏ đúng đỉnh thứ $k+1$, trả về $B'$ và đúng $k$ đỉnh. |


### 1.3. Algorithm 3: BMSSP(l, B, S) (Paper Trang 10)

| Dòng Paper | Mã giả Paper | C++ `lcs147` (`bmssp-worst-case.hpp`) | Rust `logtwothirds` (`src/bmssp.rs`) | Đánh giá & Ghi chú |
| :---: | :--- | :--- | :--- | :--- |
| **L1-3**| `if l = 0 then return BaseCase(B, S)` | `if(l == 0) return baseCase(B, S[0]);` (dòng 617) | `if l == 0 return self.base_case(b, s);` (dòng 517) | Trường hợp suy biến về BaseCase. |
| **L4** | $P, W \leftarrow \text{FindPivots}(B, S)$ | `auto [P, bellman_vis] = findPivots(B, S);` (dòng 619) | `let (p, w_order) = self.find_pivots(b, s);` (dòng 522) | Tìm Pivots và tập bao đóng Bellman-Ford. |
| **L5** | $D.\text{Initialize}(M, B)$ với $M = 2^{(l-1)t}$ | `auto &D = Ds[l - 1]; D.initialize(batch_size, B);` (621-623) | `let mut d = BlockDs::new(m_cap, b);` (dòng 532) | Khởi tạo cấu trúc dữ liệu Lemma 3.3. |
| **L6** | $D.\text{Insert}(\langle x, \hat{d}[x]\rangle)$ cho mỗi $x \in P$ | `for(int p: P) D.insert(getDist(p));` (dòng 624) | `for &x in &p { d.insert(x, kx); }` (dòng 534-539) | Nạp toàn bộ Pivots vào cấu trúc $D$. |
| **L7** | $i \leftarrow 0; B'_0 \leftarrow \min_{x \in P} \hat{d}[x]; U \leftarrow \emptyset$ | `last_complete_B = B; min(getDist(p))` (626-628) | `let bp0 = p.iter().min().unwrap_or(b);` (542) | Khởi tạo mốc $B'_0$ nếu $P = \emptyset \implies B'_0 = B$. |
| **L8** | `while \|U\| < k·2^(lt) and D non-empty` | `while(complete.size() < quota && D.size())` (632) | `while (u_order.len() < bound_cap) && !d.is_empty()` (557) | Vòng lặp chính với chặn khối lượng $k \cdot 2^{lt}$. |
| **L10** | $B_i, S_i \leftarrow D.\text{Pull}()$ | `auto [trying_B, miniS] = D.pull();` (dòng 633) | `let (si, bi) = d.pull(&mut self.rng);` (dòng 559) | Rút batch tối đa $M$ phần tử và cận chặn $B_i$. |
| **L11** | $B'_i, U_i \leftarrow \text{BMSSP}(l-1, B_i, S_i)$ | `bmsspRec(l - 1, trying_B, miniS);` (dòng 635) | `self.bmssp(l - 1, bi, &si_fresh);` (dòng 574) | Đệ quy tầng dưới; Rust lọc `!settled[x]`. |
| **L12** | $U \leftarrow U \cup U_i$ | `complete.insert(nw_complete)` (dòng 640) | `u_set.insert(x); u_order.push(x)` (580-585) | Gom các đỉnh mới hoàn thành vào $U$. |
| **L13-16**| Duyệt cạnh từ $U_i$, relax $\hat{d}[u]+w \le \hat{d}[v]$ | `for(int u: nw_complete) ... if(new <= old)` (646-652) | `for &u in &ui ... if self.try_relax(u, v, w)` (590-597) | Thư giãn toàn bộ cạnh xuất phát từ $U_i$. |
| **L17-18**| Nếu $\hat{d}[v] \in [B_i, B) \implies D.\text{Insert}(v)$ | `if(trying_B <= new_dist && new_dist < B) D.insert` (653) | `if bi <= vkey && vkey < b { d.insert }` (600-603) | Đưa các đỉnh thuộc batch tương lai vào $D$. |
| **L19-20**| Nếu $\hat{d}[v] \in [B'_i, B_i) \implies K \leftarrow K \cup \{v\}$ | `else if(complete_B <= new_dist < trying_B) can_prepend` (655) | `else if bp_i <= vkey && vkey < bi { kk.push }` (604-607) | Gom các đỉnh rơi vào khoảng trống do con dừng sớm. |
| **L21** | $D.\text{BatchPrepend}(K \cup \text{unfinished } S_i)$ | `can_prepend.push(miniS); D.batchPrepend` (661-666) | `prepend.push(si_fresh); d.batch_prepend` (613-625) | Đưa $K$ và phần chưa xử lý của $S_i$ về đầu $D$. |
| **L22** | Return $B' \leftarrow \min(B'_i, B)$, $U \leftarrow U \cup (W < B')$| `for(int x: bellman_vis) if(...) complete.push` (670-678) | `bp = min(bp_last, b); sweep W < bp` (627-635) | Bổ sung các đỉnh hoàn thành từ Bellman-Ford $< B'$. |

---

### 1.4. Lemma 3.3: Cấu trúc dữ liệu BlockDS (`batchPQ` / `BlockDs`)

| Yêu cầu Lemma 3.3 | C++ `lcs147` (`batchPQ`) | Rust `logtwothirds` (`BlockDs`) | Nhận xét Fidelity |
| :--- | :--- | :--- | :--- |
| **Hai chuỗi khối riêng biệt $D_0, D_1$** | `vector<block> D0, D1` (dòng 55-56) | `d0: VecDeque<Block>, d1: VecDeque<Block>` | Chuẩn xác theo thiết kế Lemma 3.3. |
| **Kích thước khối tối đa $M$** | Cắt khối qua median khi vượt $M$ | Quickselect median, chia 2 nửa $\le M$ | Đúng cận $O(M)$ cho mỗi phép split. |
| **Cây tìm kiếm quản lý cận các khối $D_1$** | `std::set<bound_entry>` (Red-Black tree) | Sorted bounds vector hoặc BST | Phục vụ tìm khối trong $O(\log(N/M))$. |
| **Bản ghi tra cứu vị trí (`Registry`)** | `where_is0`, `where_is1` (mảng tĩnh $O(n)$) | `registry: FxHashMap<u32, (Key, BlockId, Slot)>`| Cho phép xóa/thay thế phần tử trong $O(1)$. |
| **Precondition của `BatchPrepend`** | Mọi giá trị nạp vào phải nhỏ hơn toàn bộ $D$ | Kiểm tra `debug_assert!(v < cur_min)` | Đảm bảo tính đơn điệu của chuỗi $D_0$. |


---

## 2. Năm kiểm tra hồi quy do dự án lựa chọn (không phải mục §7 của PDF)

PDF `2504.17033v2` không có tiêu đề §7 “What this is not”. Năm kiểm tra sau đây được tập hợp từ audit/repo để khảo sát một số lỗi triển khai có thể xảy ra; chúng không phải năm regression test do tác giả bài báo liệt kê và không chứng nhận thuật toán.

### R1. Thoái hóa thành Dijkstra (Pull biến thành heap / $M = 1$ khắp nơi)
* **Ý nghĩa:** Nếu thuật toán chỉ rút từng đỉnh nhỏ nhất ($M=1$) hoặc mảng sự kiện hoàn thành luôn được sắp thứ tự theo khoảng cách thì thuật toán chỉ là Dijkstra thông thường.
* **Kiểm chứng:** File `tests/not_dijkstra.rs` ép thông số $(k, t) = (2, 2)$ trên đồ thị ngẫu nhiên $n = 500 \dots 1.500$.
* **Kết quả:** Trên 20 seed kiểm tra, có **20/20 seed thứ tự settlement log KHÔNG đơn điệu toàn cục** (pass điều kiện tối thiểu $\ge 15/20$). BMSSP thực sự xử lý theo từng khối phân vùng chứ không duyệt đỉnh theo thứ tự khoảng cách tăng dần như Dijkstra.

### R2. Bỏ qua FindPivots ($P \equiv S$)
* **Ý nghĩa:** Nếu luôn gán $P = S$ thì không có sự giảm số lượng nguồn, vi phạm trực tiếp cận lý thuyết $O(m \log^{2/3} n)$.
* **Kiểm chứng:** Kiểm tra cấu trúc rừng cạnh căng $F$ và quy tắc chọn Pivot có kích thước cây con $\ge k$.
* **Kết quả:** `tests/test_find_pivots.py` và mã Rust `bmssp.rs` (dòng 363-414). Nhánh dừng sớm chỉ kích hoạt khi $|W| > k|S|$, các trường hợp còn lại đều duyệt cây con và trả về tập $P \subset S$ có kích thước $|P| \le |W|/k$.

### R3. Vòng lặp $k$-round chạy cạn kiệt (Thiếu dừng sớm)
* **Ý nghĩa:** Vòng lặp Bellman-Ford nếu luôn chạy đủ $k$ vòng mà không kiểm tra $|W| > k|S|$ sẽ tốn kém chi phí không cần thiết trên đồ thị dày hoặc phân nhánh lớn.
* **Kiểm chứng:** Vị trí điều kiện dừng sớm `if |W| > k|S|` phải nằm **bên trong** vòng lặp $i = 1 \dots k$.
* **Kết quả:** Cả C++ (dòng 568) và Rust (dòng 363) đều đặt lệnh thoát sớm ngay sau mỗi round $i$, bảo đảm độ phức tạp $O(k|W|) = O(k^2|S|)$ của Lemma 3.2.

### R4. Thay thế BatchPrepend bằng các lệnh Insert lặp lại
* **Ý nghĩa:** Lemma 3.3 đòi hỏi $D_0$ riêng để đạt chi phí $O(|L| \log(|L|/M))$. Nếu thay bằng các lệnh `Insert` thông thường vào $D_1$, chi phí sẽ đội lên $O(|L| \log(N/M))$, làm hỏng cận thời gian.
* **Kiểm chứng:** Sự tồn tại của chuỗi khối $D_0$ riêng biệt và bộ đếm `ds_prepend_items`.
* **Kết quả:** Thống kê trên benchmark cho thấy `ds_prepend_items > 0` trên tất cả đồ thị lớn có phân rã đệ quy. Precondition $K < \min(D)$ được xác nhận nghiêm ngặt.

### R5. Loại bỏ điều kiện chặn Partial Execution
* **Ý nghĩa:** Nếu chạy vòng lặp cho đến khi $D$ rỗng mà không chặn $|U| < k \cdot 2^{lt}$, hoặc BaseCase không cắt ở $k+1$, thuật toán sẽ xử lý quá khối lượng cho phép của một tầng đệ quy.
* **Kiểm chứng:** Điều kiện dừng `while |U| < k·2^(lt)` ở BMSSP và $|U_0| < k+1$ ở BaseCase.
* **Quan sát lịch sử:** Hai mốc chặn được ghi nhận trong snapshot và các test tương ứng đã chạy. Điều đó chưa tự xác nhận mọi tiền/hậu điều kiện của Lemma 3.9.

---

## 3. PHÂN TÍCH BẤT BIẾN (INVARIANTS) VÀ HỢP ĐỒNG THUẬT TOÁN

### 3.1. Hợp đồng Lemma 3.2 (FindPivots)
* **Precondition:** Mọi đỉnh chưa hoàn thành $v$ có $d(v) < B$ đều có đường đi ngắn nhất đi qua một đỉnh hoàn thành trong $S$.
* **Postcondition:** Trả về $P \subseteq S$ và $W \subseteq \tilde{U}$ sao cho $|W| = O(k|S|)$ và $|P| \le |W|/k$. Với mọi $x \in \tilde{U}$, hoặc $x \in W$ đã hoàn thành, hoặc đường đi ngắn nhất tới $x$ đi qua một pivot $y \in P$.
* **Đánh giá C++ vs Rust:**
  * **C++:** Dùng kỹ thuật lan nhãn `root[v] = root[u]` ngay khi relax. Nếu ở bước $k$ đỉnh $v$ bị relax bởi một cây khác, con cái của $v$ chưa kịp cập nhật `root`. Tuy nhiên, vì $\sum \text{treesz} = |vis| \le k|S|$, việc đếm này chỉ có thể làm giảm kích thước ước lượng của cây, dẫn đến việc chọn Pivot an toàn hơn (không vi phạm cận trên).
* **Rust:** Dựng cây từ `pred` sau $k$ rounds theo snapshot được mô tả. Chưa có bằng chứng đủ để kết luận mọi điều kiện của Lemma 3.2 được bảo toàn.

### 3.2. Hợp đồng BaseCase (Lemma 3.9)
* **Precondition:** $S = \{x\}$ singleton, $x$ đã hoàn thành, $B > \hat{d}[x]$.
* **Postcondition:** Trả về $B' \le B$ và $U = T_{<B'}(S)$ hoàn thành, $|U| \le k$. Nếu $B' < B$ thì $|U| \ge k$.
* **Quan sát mã:** Cả hai có min-heap lazy deletion và nhánh cutoff được mô tả. Việc lấy đúng tập duy nhất, bound và hậu điều kiện cần xác minh riêng; không gọi đây là bảo đảm “bit-exact”.

### 3.3. Finding F3 được báo cáo upstream — chưa xác nhận là lỗ hổng của paper
Một audit trong repo `logtwothirds` nêu giả thiết về khóa trong $D$ và settled vertices. Đây là nhận định của tác giả audit, không phải kết luận độc lập đã chứng minh rằng PDF sai. Cần đối chiếu chính xác lemma, điều kiện queue/update và mã theo commit. Bộ lọc `settled` trong một implementation có thể là biện pháp bảo vệ; sự hiện diện của nó không tự xác nhận hay bác bỏ chứng minh paper. Trạng thái: **OPEN / NEEDS PROOF REVIEW**.


---

## 4. BÓC TÁCH THỰC NGHIỆM VẤN ĐỀ O-1 VÀ SAI SỐ LÀM TRÒN SỐ THỰC

### 4.1. Phản ví dụ Near-Tie và Hiện tượng phân kỳ
Thử nghiệm trên đồ thị 3 đỉnh (`near_tie_test.txt`):
* Cạnh $0 \to 1$ có trọng số $4 \times 10^{-11}$.
* Cạnh $0 \to 2$ có trọng số $0.0$.
* Cạnh $2 \to 1$ có trọng số $0.0$.

Khoảng cách lý thuyết ngắn nhất: $0 \to 2 \to 1$ có chi phí $0.0 < 4 \times 10^{-11}$. Đường đi tối ưu đến đỉnh 1 phải có tiền bối là $2$ (`pred[1] = 2`).

### 4.2. Cơ chế sinh lỗi của C++ Upstream (`lcs147`)
Trong `bmssp-worst-case.hpp`, kiểu `uniqueDistT` thực hiện hàm làm tròn:
```cpp
static constexpr wT SCALE = 1e10;
static constexpr wT SCALE_INV = ((wT) 1.0) / SCALE; 

static inline wT sanitize(wT w) {
    if constexpr (std::is_floating_point_v<wT>) {
        return std::round(w * SCALE) * SCALE_INV;
    }
    return w;
}
```
1. Khi cạnh $0 \to 1$ thư giãn đỉnh 1: `d[1] = 4e-11`, `hops[1] = 1`, `pred[1] = 0`. Khóa tuple là `(sanitize(4e-11), 1, 1, 0) = (0.0, 1, 1, 0)`.
2. Khi cạnh $2 \to 1$ cố gắng thư giãn đỉnh 1 với khoảng cách thực $0.0$: Khóa ứng viên là `(sanitize(0.0), 2, 1, 2) = (0.0, 2, 1, 2)`.
3. Phép so sánh tuple: Phần tử thứ nhất bằng nhau (`0.0 == 0.0`). Thuật toán chuyển sang so sánh số hop: ứng viên có 2 hops, hiện tại có 1 hop. Do $2 \le 1$ là **SAI**, phép thư giãn bị **TỪ CHỐI**!
4. Kết quả: Đỉnh 1 bị giữ nguyên khoảng cách `4e-11` và tiền bối `0`, làm sai lệch kết quả tối ưu.

### 4.3. Khác biệt thứ tự khóa Rust không đồng nghĩa số học chính xác
Rust không nhân với bất kỳ hệ số scale tùy tiện nào. Khóa `Key` trong Rust được định nghĩa:
```rust
pub struct Key {
    pub len: f64,
    pub hops: i32,
    pub id: i64,
}
```
Rust so sánh giá trị double bằng `f64::total_cmp`; điều đó cho thứ tự toàn phần trên các giá trị float, nhưng phép cộng `f64` vẫn là phép tính dấu phẩy động hữu hạn, không phải số thực chính xác. Các ca kiểm thử có thể xác nhận hành vi trên đầu vào cụ thể, không chứng minh miễn nhiễm với mọi sai số/near-tie.

### 4.4. Lỗi O-1 trong snapshot visualizer đã review
Trong runner của visualizer (`native/bmssp-traced.hpp`), hàm `sanitize` đã được vá thành `return w;`. Tuy nhiên, lỗi O-1 trong báo cáo cũ xuất hiện vì runner thực hiện thêm một lượt **BFS hậu xử lý với dung sai $10^{-9}$** để tái tạo cây đường đi ngắn nhất, dẫn đến việc xem $4 \times 10^{-11}$ và $0.0$ là tương đương nhau.
* **Trạng thái theo thời gian:** Đây là lỗi được review tái hiện trong snapshot 14/11. Root báo sau review đã cập nhật solver/verifier và chạy 31 test; xác nhận kỹ thuật chi tiết được ghi ở phần “sau sửa” do root bổ sung. Kết quả này đóng regression cụ thể, không chứng nhận mọi đường đi float hay fidelity tổng quát. BFS với dung sai trong snapshot cũ không chứng nhận đường đi tối ưu chính xác.

---

## 5. Trạng thái các câu hỏi fidelity (không chấm điểm)

| Tiêu chí | C++ (`lcs147`) | Rust (`logtwothirds`) | Kết luận |
| :--- | :---: | :---: | :--- |
| **FindPivots / Lemma 3.2** | Có vector active và lan nhãn root; equivalence với set/forest semantics chưa chứng minh | Có pred/children reconstruction; hợp đồng chưa chứng nhận | **OPEN** |
| **BaseCase / Lemma 3.9** | Có cutoff k+1 và lazy heap; uniqueness/cost cần kiểm tra | Có cutoff k+1 và lazy heap; uniqueness/cost cần kiểm tra | **OPEN** |
| **BMSSP recursion** | Có các nhánh queue tương ứng; set contracts chưa xác minh đầy đủ | Có các nhánh tương ứng; paper mapping và settled behavior cần review | **OPEN** |
| **BlockDS / Lemma 3.3** | D0/D1 quan sát được; amortization chưa chứng nhận | Randomized quickselect, vector insertion/removal; worst-case cận bài chưa chứng nhận | **OPEN / NOT CERTIFIED** |
| **Số học và đường đi** | Upstream rounding và visualizer tolerance có phản ví dụ lịch sử | `total_cmp` cho thứ tự khóa, không làm double thành số thực chính xác | Kiểm thử theo miền đã nêu; exact/tolerance cần tách |
| **Test hữu hạn** | Báo cáo lịch sử 100 graph | Báo cáo lịch sử >3,000 graph | Không chứng minh tổng quát; cần tái lập cấu hình/kết quả |

### Cập nhật trạng thái các vấn đề:
* **O-3:** OPEN / NOT CERTIFIED. Mapping và test là bằng chứng hỗ trợ, không chứng minh mọi bất biến.
* **O-1:** Snapshot visualizer có lỗi đường đi near-tie được tái hiện; sửa chữa cần xác nhận bằng regression. `total_cmp` không đồng nghĩa tổng double chính xác.
* **O-4:** OPEN / NOT CERTIFIED. Hai chuỗi quan sát được không tự chứng minh amortized bound của mọi thao tác hoặc toàn implementation.

