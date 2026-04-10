# HƯỚNG DẪN CÀI ĐẶT VÀ CHẠY WEB DASHBOARD CHO XE AGV 🚗
*(Dành cho bạn bè - Không yêu cầu kiến thức lập trình)*

Chào bạn! Hệ thống Web điều khiển xe tự hành này đã được thiết kế sẵn để chạy "chỉ bằng 1 nút bấm" qua Docker. Bạn chỉ cần làm theo các bước siêu ngắn gọn dưới đây thôi nhé.

---

## BƯỚC 1: Tìm cổng USB kết nối với Xe (ESP32 / Arduino)

1. **Cắm mạch của xe vào máy tính.**
2. **Xác định cổng USB (Cổng COM):**
   - **Đối với Windows:** Bạn bấm chuột phải vào nút Start (biểu tượng Windows) -> Chọn **Device Manager** -> Kéo xuống tìm mục **Ports (COM & LPT)**. Ở đó sẽ ghi là `COMX` (Ví dụ `COM3`, `COM5`, v.v...). Hãy ghi nhớ số này.
   - **Đối với Macbook:** Thường nó sẽ là tên dạng `/dev/tty.usbserial-0001`
   - **Đối với Linux:** Thường là `/dev/ttyUSB0`

3. Mở file `.env.example` trong thư mục này lên (bằng Notepad hoặc bất cứ phần mềm đọc chữ nào), đổi đúng tên cổng mà bạn vừa tìm được ở cấu hình `AGV_SERIAL_PORT` và Lưu Lại. 
   > Sau khi lưu xong, hãy Đổi tên file này thành **`.env`** nhé! (Xóa chữ `.example` đi).

---

## BƯỚC 2: Chạy hệ thống bằng Docker

Hệ thống đã được đóng gói mọi thứ (web, server, thư viện). Bạn không cần cài thêm gì ngoài Docker.

1. Hãy chắc chắn máy bạn đã bật ứng dụng **Docker Desktop**.
2. Nhấn vào thanh đường dẫn thư mục (Folder path) ở trên cùng của màn hình cửa sổ chứa các tập tin dự án này, gõ chữ `cmd` và ấn Enter. Thao tác này sẽ mở màn hình đen (Command Prompt) tại đúng vị trí của dự án.
3. Gõ câu lệnh thần thánh sau vào rồi ấn Enter:

   ```bash
   docker-compose up -d --build
   ```

4. Đợi một phút để Docker tự "vào bếp" nấu code cho bạn. Khi nó chạy xong, màn hình sẽ báo `Started agv-dashboard`.

---

## BƯỚC 3: Mở bảng điều khiển (Dashboard)

1. Mở trình duyệt Web của bạn (Chrome / Safari / Cốc Cốc...).
2. Truy cập vào đường link sau: **http://localhost:3000**
3. Tèn ten! Giao diện vạch tốc độ, vòng xoay động cơ và thẻ RFID đã hiện ra. Chỉ cần quẹt thẻ trên xe, thông số sẽ nhảy thẳng lên màn hình!

---

💡 **XỬ LÝ SỰ CỐ (Dành riêng cho máy Windows)**
Vì Docker trên Windows thỉnh thoảng rất "khó ở" trong việc nhận diện cổng USB cắm vào máy. Nếu bạn đã hoàn thành BƯỚC 3, màn hình Web mở lên được rồi nhưng xe quẹt thẻ không thấy hiện thông số trên Web, thì hãy làm cách sau (Bỏ qua Docker):

1. Cài phần mềm tên là **NodeJS** (Cứ lên mạng tải tiếp Next Next là xong).
2. Mở lại màn hình đen `cmd` ở ngay thư mục này.
3. Gõ: `npm install` (Đợi 1 tí)
4. Gõ: `npm start`
5. Khởi động lại trang Web là OK!

Chúc bạn thành công!
