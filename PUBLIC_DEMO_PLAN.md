# Kế hoạch Triển khai Public Demo BMSSP (Cloudflare Tunnel & Giữ Máy Cá nhân)

Cập nhật: 15/11/2026  
Môi trường: Windows / PowerShell / Local Server (Port 8765 & 8766)  
Trạng thái: **Đang lập kế hoạch (0%) — Chờ người dùng phê duyệt**

---

## 1. Mục tiêu & Bối cảnh

Người dùng muốn công khai (public) các bản demo Visualizer của dự án BMSSP (DMM+25) ra Internet thay vì chỉ chạy cục bộ (localhost).
- **Phương thức vận hành:** Treo máy cục bộ (self-hosted / home lab server).
- **Yêu cầu cốt lõi:**
  1. Có giải pháp tăng tốc độ truyền tải, tối ưu ping/độ trễ (giảm RTT) từ xa thông qua mạng biên Cloudflare.
  2. Không cần mở cổng modem (No Port Forwarding), an toàn tuyệt đối cho IP gia đình (vượt qua CGNAT).
  3. Đảm bảo hỗ trợ Server-Sent Events (SSE) thời gian thực không bị trễ hoặc dồn cục bộ nhớ đệm (buffer bloat).
  4. Cơ chế bảo vệ máy trạm cá nhân: chống tràn CPU / DoS khi có người dùng bên ngoài kích hoạt giải thuật toán nặng.

---

## 2. Kiến trúc Giải pháp Kỹ thuật

```
[ Người dùng Internet ] 
          │  (HTTP/3 QUIC / TLS 1.3, Anycast DNS)
          ▼
   ┌────────────────────────────────────────┐
   │        Cloudflare Global Edge          │
   │  - 300+ PoPs toàn cầu (gần khách nhất) │
   │  - DDoS Protection & WAF               │
   │  - SSL/TLS Termination tại Edge        │
   │  - TCP BBR Congestion Control          │
   └────────────────────────────────────────┘
          │  (Cloudflare Tunnel - Mã hóa kép qua QUIC)
          ▼
   ┌────────────────────────────────────────┐
   │        Máy Cá nhân (Windows)           │
   │                                        │
   │  [cloudflared daemon]                  │
   │         │                              │
   │         ├──► Port 8765: C++ Visualizer │
   │         └──► Port 8766: Rust Mizchi Lab│
   └────────────────────────────────────────┘
```

### Tại sao chọn Cloudflare Tunnel (`cloudflared`)?
1. **Tối ưu hóa Ping & Tốc độ tải (Edge Network):**
   - Khách truy cập kết nối đến PoP Cloudflare gần họ nhất (Việt Nam có PoP tại Hà Nội và TP.HCM).
   - Bắt tay TLS 1.3 và HTTP/3 chỉ mất 0-RTT/1-RTT tới Edge.
   - Kết nối giữa Cloudflare Edge và máy local duy trì qua luồng ngầm QUIC liên tục, loại bỏ thời gian bắt tay TCP nhiều lần.
2. **Che giấu 100% IP nhà:** Không để lộ địa chỉ IP thật, ngăn chặn hoàn toàn việc bị dò quét cổng hoặc tấn công vào mạng nội bộ gia đình.
3. **Không cần IP tĩnh hay mở cổng Modem:** Hoạt động ổn định ngay cả với mạng di động (4G/5G) hoặc nhà mạng dùng CGNAT.

---

## 3. Hai Phương án Triển khai Cloudflare

| Tiêu chí | Phương án 1: Quick Tunnel (Khuyên dùng thử nghiệm) | Phương án 2: Named Tunnel (Chuẩn sản xuất) |
| :--- | :--- | :--- |
| **Tên miền** | Miễn phí ngẫu nhiên (`https://<id>.trycloudflare.com`) | Cần sở hữu tên miền riêng (VD: `bmssp.yourdomain.com`) |
| **Tài khoản** | **Không cần tài khoản**, không cần đăng nhập | Cần tài khoản Cloudflare miễn phí + thêm domain |
| **Thời gian triển khai** | **5 phút** (chạy 1 lệnh duy nhất) | 15–20 phút (cần cấu hình DNS và Zero Trust Token) |
| **Tính cố định URL** | Đổi URL mỗi khi bật lại tunnel | URL cố định vĩnh viễn |
| **Tính năng bổ sung** | SSL tự động, CDN cơ bản, SSE passthrough | Gán bảo vệ bằng OTP/Email (Cloudflare Access), Rate Limit sâu |

---

## 4. Các Giai đoạn Thực hiện & Phân bổ Trọng số

### Giai đoạn 1: Khảo sát & Chuẩn bị Môi trường (Trọng số: 15%)
- Tải công cụ `cloudflared.exe` chính thức từ Cloudflare cho Windows 64-bit.
- Kiểm tra tính tương thích và quyền thực thi trên PowerShell.
- Thống nhất phương án (Quick Tunnel hay Named Tunnel).

### Giai đoạn 2: Gia cố Backend & Giới hạn Tài nguyên Máy chủ (Trọng số: 25%)
- **Chống quá tải CPU:**
  - Thiết lập semaphore giới hạn: tối đa 2 tác vụ thuật toán chạy đồng thời; các yêu cầu tiếp theo trả về mã `429 Too Many Requests`.
  - Giới hạn kích thước đồ thị nhận từ bên ngoài ($n \le 1.000$ đỉnh) để tránh ai đó upload đồ thị khổng lồ gây treo máy.
  - Thiết lập timeout tối đa 5 giây cho mỗi lần chạy native.
- **Tối ưu hóa Server-Sent Events (SSE):**
  - Bổ sung header `X-Accel-Buffering: no` và `Cache-Control: no-cache, no-transform` vào `server.py` của cả 2 visualizer để Cloudflare không giữ đệm (buffer) sự kiện hoạt ảnh.

### Giai đoạn 3: Cấu hình Định tuyến & Khởi chạy Cloudflare Tunnel (Trọng số: 25%)
- Xây dựng file cấu hình định tuyến cho cả 2 cổng:
  - Port `8765`: C++ Visualizer (lcs147).
  - Port `8766`: Rust Mizchi Lab (DMM+25 / C-HD / Dijkstra).
- Cấu hình một Cổng chuyển hướng (Landing Page/Gateway) hoặc cấp 2 link truy cập rõ ràng.
- Khởi chạy daemon `cloudflared` kết nối ra Cloudflare Edge.

### Giai đoạn 4: Kiểm thử Thực địa & Đo đạc Độ trễ (Trọng số: 20%)
- Dùng thiết bị ngoài mạng nội bộ (mạng 4G di động) truy cập vào link public:
  - Kiểm tra giao diện và hiển thị đồ thị SVG/Canvas.
  - Chạy thử chế độ Live Demo / Controlled Streaming SSE (đảm bảo từng bước hoạt ảnh hiển thị mượt mà không bị khựng).
  - Đo lường Ping (RTT) và thời gian phản hồi (TTFB).

### Giai đoạn 5: Tự động hóa "Treo máy" & Đóng gói Vận hành (Trọng số: 15%)
- Viết script quản trị `public_server.py` hoặc script PowerShell 1-click:
  - Tự động bật đồng thời cả 2 server local (8765, 8766) và tunnel.
  - Cơ chế tự hồi phục (auto-restart/watchdog) nếu tiến trình server hoặc tunnel bị ngắt.
- Cung cấp lệnh tắt an toàn và hướng dẫn cấu hình Windows không vào chế độ Sleep khi gập máy/treo máy.

---

## 5. Tiêu chí Hoàn thành (Definition of Done)
1. Có link HTTPS công khai truy cập được từ điện thoại/máy tính bên ngoài Internet.
2. Tốc độ ping/phản hồi nhanh, ổn định nhờ mạng biên Cloudflare.
3. Chạy được đầy đủ tính năng: Fast run, Live controlled trace (SSE mượt mà), xuất kết quả.
4. Máy chủ cá nhân được bảo vệ: không bị treo máy nếu có nhiều người bấm liên tục.
5. Có tài liệu và script quản trị bật/tắt rõ ràng.
