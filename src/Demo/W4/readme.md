# Hướng dẫn chạy code

**Biên dịch và chạy chương trình**

1. Môi trường biên dịch
- Hệ điều hành: Linux, macOS hoặc Windown sử dụng WSL
- Trình biên dịch: GCC
- Mã nguồn gồm 2 file: tcp_server.c và tcp_client.c
2. Biên dịch và chạy chương trình
- Mở terminal và di chuyển đến thư mục chứa hai file mã nguồn.
**Bước 1: Khởi chạy TCP Server**
- Mở Terminal thứ nhất và nhập lệnh: gcc -Wall -Wextra tcp_server.c -o tcp_server && ./tcp_server
=> Server sẽ bắt đầu lắng nghe và chờ kết nối từ client
**Bước 2: Khởi chạy TCP Client**
- Mở Terminal thứ hai và nhập lệnh: gcc -Wall -Wextra tcp_client.c -o tcp_client && ./tcp_client
=> Client sẽ kết nối đến Server.
**Bước 3: Gửi và nhận dữ liệu**
- Nhập message muốn gửi tại Terminal của Client
- Client gửi thông điệp đến Server thông qua giao thức TCP
- Server nhận được dữ liệu và gửi lại dữ liệu Server vừa nhận được
- Nhập "exit" hoặc Ctrl+C để thoát chương trình
3. Cấu hình kết nối
- Giao thức TCP
- Địa chỉ Server: 127.0.0.1
- Port: 9090

Client và Server cùng chạy trên một máy tính thông qua địa chỉ loopback
