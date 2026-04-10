# HƯỚNG DẪN CÀI ĐẶT VÀ CHẠY WEB DASHBOARD CHO XE AGV 🚗
*(Dành cho bạn bè - Không yêu cầu kiến thức phức tạp)*

Chào bạn! Hệ thống này gồm 2 phần: nạp code điều khiển vào mạch ESP32 (Xe) và giao diện Web bật lên bằng Docker. Hãy làm theo đúng từng bước sau nhé!

---

## BƯỚC 1: Nạp Code Chạy Xe qua phần mềm Arduino IDE
1. Kết nối xe vào cổng USB của máy tính.
2. Mở file `agv_pi.ino` bằng phần mềm **Arduino IDE**.
3. Chọn đúng loại Board (ESP32) và thiết lập Cổng (Port).
4. Bấm **Upload** (Mũi tên chĩa sang phải) để nạp code. 
> 🛑 **RẤT QUAN TRỌNG:** Ở góc trên bên phải của Arduino IDE có tính năng **Serial Monitor** (hình kính lúp). Nếu bạn dùng màn hình này để thử xe, hãy chắc chắn **PHẢI ĐÓNG KÍN MÀN HÌNH ĐÓ LẠI** trước khi sang Bước 2! Nếu không, Cổng kết nối sẽ bị chiếm dụng và Giao diện Web không thể kết nối tới xe được.

---

## BƯỚC 2: Cài cổng kết nối cho Giao diện Web
1. Dựa vào Cổng Port bạn vừa chọn trong Arduino IDE (Ví dụ: `COM3`, `COM5` bên Windows; hoặc dạng `/dev/ttyUSB0` bên MacOS/Linux).
2. Mở file `.env.example` trong thư mục này lên (bằng Notepad), đổi chữ sau dấu `=` thành đúng cổng kết nối của bạn.
   - Ví dụ: `AGV_SERIAL_PORT=COM3`
3. Lưu lại và **Đổi tên file đó thành `.env`** (Xóa đuôi `.example` đi).

---

## BƯỚC 3: Mở Web Dashboard bằng Docker
Giao diện và Code xử lý đã được đóng gói chung vào thùng container mang tên Docker cực kỳ gọn gàng.

1. Khởi động phần mềm **Docker Desktop** (Hãy mở nó lên và để nó chạy nền).
2. Hãy vào thanh trên cùng chỉ địa chỉ của thư mục (folder chứa project này), gõ chữ `cmd` và ấn Enter. Nếu dùng Mac thì ấn chuột phải vào folder chọn "Open in Terminal".
3. Copy và dán chạy đúng 1 câu thần chú sau:

   ```bash
   docker-compose up -d --build
   ```

4. Chờ 30 giây ráng cho Docker tự nấu nướng. Khi đã báo `Started agv-dashboard` nghĩa là bạn đã thành công!

---

## BƯỚC 4: Lái xe trên Web thôi!
1. Mở bất kỳ trình duyệt Web nào (Chrome / Safari...)
2. Truy cập vào đường link: **http://localhost:3000**
3. Giao diện Cyberpunk đẹp mắt sẽ hiện ra. Nếu bạn đã Đóng Serial Monitor ở Bước 1 đúng cách, chữ **Connecting...** trên góc màn hình Web sẽ chuyển màu xanh nhấp nháy thành chữ **Connected**, và dữ liệu hai bánh xe sẽ hiện lên mượt mà theo từng chu kỳ 20ms của xe!

---

💡 **TIP XỬ LÝ NHANH TRÊN MÁY TÍNH WINDOWS**
Nếu Web lên hình rồi mà trạng thái báo Disconnected hoài (Do Docker trên Windows thỉnh thoảng giấu đi cổng USB). Hãy xử lý thủ công không cần Docker cực nhanh:
- Lên mạng tải và cài phần mềm tên là **NodeJS** (Cứ Next là xong).
- Mở lại màn hình đen `cmd` ngay trong thư mục này.
- Gõ: `npm install`
- Gõ: `npm start`
- Tải lại trang web (F5), 100% sẽ hoạt động chớp nhoáng! 

Chúc bạn điều khiển xe ngầu nhất nhé!
