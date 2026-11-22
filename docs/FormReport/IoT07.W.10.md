
# FORM BÁO CÁO THEO TUẦN

**ĐỊNH HƯỚNG ĐỀ TÀI:** QUIC-SIMULATION

**Người HƯỚNG DẪN:** Đặng Hữu Phúc

**NHÓM:** IoT.07

**TUẦN:** 10

**NGƯỜI NỘP:** Nguyễn Hoàng Anh - Nguyễn Lương Đức

---

### 1. Cam kết tuần trước
1. **Mục tiêu 1:** Xây dựng lại phần mềm giao tiếp kết nối với môi trường ảo Mininet
- Xây dựng chương trình truyền nhận dữ liệu độc lập bằng C 
- Triển khai trên các host Mininet và kiểm soát các điều kiện mạng để tạo nền tảng cho việc thử nghiệm giao thức

2. **Mục tiêu 2:** MQTT over QUIC
- Tích hợp MQTT over QUIC vào chương trình client
- Triển khai EMQX broker và thiết lập cơ chế publish/subscribe để truyền dữ liệu cảm biến giữa nhiều client và server

3. **Mục tiêu 3:** Xây dựng hệ thống kiểm chứng và đánh giá hiệu năng truyền thông
- Sử dụng Wireshark, log ứng dụng và công cụ giám sát tài nguyên để xác minh quá trình truyền nhận
- Đo các chỉ số hiệu năng và đánh giá hệ thống dưới những điều kiện mạng khác nhau

### 2. Kết quả / Minh chứng

- [ ] Hoàn thành
- [X] Hoàn thành một phần
- [ ] Chưa hoàn thành

#### Mô tả ngắn kết quả và minh chứng
- Phần mềm giao tiếp và Mininet: Nhóm đã xây dựng hai chương trình C độc lập. Publisher chạy trên h1 và subscriber chạy trên h2. Script topology tạo hai cặp veth nối h1 và h2 với OVS bridge s1, rồi tạo internal port mn-gw có IP 10.78.0.254/24 để các host Mininet truy cập broker qua Ubuntu.
- MQTT over QUIC: Hai client sử dụng ngtcp2 và GnuTLS để kết nối tới EMQX chạy trong Docker qua cổng UDP 14567. Publisher trên h1 gửi dữ liệu nhiệt độ mô phỏng Temperature = 28.5 C lên topic lab/temperature. Subscriber trên h2 nhận đúng topic và nội dung bản tin.
- Kiểm chứng truyền nhận: Kịch bản tự động đã kiểm tra kết nối IP, chờ h2 đăng ký topic thành công rồi chạy publisher. Kịch bản xác nhận bản tin đến đúng. Thu file bắt gói bằng Wireshark.

### 3. Vướng mắc / Quyết định cần hỗ trợ

- [X] Không
- [ ] Thông thường – nhóm tự xử lý
- [ ] Cần bàn
- [ ] Khẩn – đang chặn tiến độ

**Mô tả:**: 
- **Vấn đề:**: 
- **Ảnh hưởng:**: 
- **Cần hỗ trợ:**: 

### 4. Cam kết tuần tới

- Mở rộng thử nghiệm trên Mininet. Chạy kịch bản với mạng mặc định, mạng có thêm độ trễ và mạng có thêm độ trễ kèm mất gói. Giữ cùng kích thước bản tin và số lần gửi để so sánh giữa các kịch bản.
- Thu minh chứng cho MQTT over QUIC. Bắt lưu lượng UDP 14567 tại interface mn-gw bằng Wireshark. Dùng file bắt gói để quan sát trao đổi QUIC và log ứng dụng để đối chiếu quá trình đăng ký topic, gửi và nhận bản tin MQTT.
- Đo và đánh giá hiệu năng. Ghi thời điểm gửi, thời điểm nhận và số bản tin nhận được. Tổng hợp độ trễ truyền nhận, tỷ lệ nhận bản tin và mức sử dụng CPU/RAM của client và broker vào bảng so sánh.

### 5. Điểm nổi bật muốn chia sẻ

- Kết quả nổi bật:
- Kiến thức hoặc công cụ mới:
- Bài học kinh nghiệm: