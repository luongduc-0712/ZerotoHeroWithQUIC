# Chạy sensor QUIC trên Mininet: h1–s1–h2

Mininet tạo các host có network namespace riêng trên cùng máy Linux. `h1` và
`h2` có IP/interface riêng; chúng vẫn dùng chung filesystem với máy thật nên
không cần chép binary hoặc chứng chỉ giữa hai host. `s1` chuyển tiếp frame như
một switch Ethernet. Trong bài này, `s1` chạy Open vSwitch ở chế độ bridge;
không cần controller SDN.

```text
h1: 10.0.0.1/24             s1                 h2: 10.0.0.2/24
sensor QUIC client -------- bridge ----------- QUIC echo server UDP/4433
```

QUIC chạy trên UDP/IP nên có thể chạy trong Mininet bằng các binary Linux hiện
có. Phải dùng `10.0.0.2` làm đích từ `h1`: `127.0.0.1` chỉ trỏ về chính host
đang chạy lệnh. Sensor sinh dữ liệu mô phỏng, chưa đọc phần cứng thật.

## 1. Chuẩn bị

Nên dùng Ubuntu/Debian Linux hoặc máy ảo Ubuntu có quyền `sudo`. Mininet cần
quyền root để tạo namespace, interface ảo và bridge; chỉ có module Python hoặc
lệnh `mn` chưa đủ nếu môi trường chặn các quyền mạng này.

```bash
sudo apt-get update
sudo apt-get install -y mininet openvswitch-switch iproute2 iputils-ping
sudo systemctl start openvswitch-switch
```

Nếu hệ thống không có systemd, dùng cách quản lý dịch vụ của hệ thống, ví dụ
`sudo service openvswitch-switch start`. Kiểm tra OVS:

```bash
sudo ovs-vsctl show
```

Chuyển vào thư mục `demo/src` của dự án rồi build và tạo chứng chỉ:

```bash
cd /home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/demo/src
make deps       # nếu chưa có ngtcp2 phù hợp; xem README.md về dependency
make -j4
make certs
make test
```

Các bước build chạy bằng tài khoản thường. Chỉ bước chạy Mininet cần `sudo`.
Chứng chỉ mặc định chứa `DNS:localhost`; runner dùng `--server-name localhost`
để xác minh danh tính này khi kết nối tới `10.0.0.2`. Địa chỉ UDP vẫn là IP của
`h2`; kiểm tra chứng chỉ vẫn được bật. Nếu dùng chứng chỉ khác, cấp SAN phù hợp
hoặc chọn `--server-name` đúng với chứng chỉ.

## 2. Chạy tự động

```bash
sudo /usr/bin/python3 scripts/mininet-demo.py
```

Runner tạo `h1–s1–h2`, thử ping, mở server trên `h2`, chờ server sẵn sàng rồi
mở sensor trên `h1`. Mặc định sensor gửi **20 mẫu, mỗi 1000 ms**, qua một kết
nối QUIC. Server echo; client kiểm tra toàn bộ dữ liệu rồi đóng kết nối.
Cuối lượt, runner dừng tiến trình và xóa topology do chính nó tạo.

Log client hiện ngay trên terminal. Log hai phía được giữ trong thư mục
`logs/mininet-.../server.log` và `client.log`, đường dẫn được in khi chạy.
Kết quả đúng có `handshake complete`, `ECHO VERIFIED`, `result=PASS` ở hai
phía và dòng cuối `PASS: h1 sensor -> s1 -> h2...`. Ping thành công chỉ chứng
minh IP thông nhau; các log này mới kiểm tra được QUIC/TLS và dữ liệu ứng dụng.

Ví dụ lấy 100 mẫu với chu kỳ 100 ms, ghi log vào thư mục mới:

```bash
sudo /usr/bin/python3 scripts/mininet-demo.py \
  --count 100 --interval-ms 100 --log-dir /tmp/quic-mininet-run1
```

Mỗi lượt cần thư mục log rỗng/mới. Trong chế độ sensor, `--timeout` là giới hạn
**không có tiến triển dữ liệu**, không phải giới hạn toàn bộ thời gian lấy mẫu.
Nó phải lớn hơn chu kỳ lấy mẫu; runner mặc định dùng 15 giây hoặc chu kỳ cộng
5 giây nếu chu kỳ dài hơn. Kết nối có thể chạy liên tục quá thời gian này nếu
vẫn gửi/nhận được dữ liệu. Server cập nhật watchdog khi nhận dữ liệu hoặc ACK.

Runner tự động có thêm giới hạn tổng thời gian `--max-runtime`: mặc định bằng
hai lần thời gian lấy mẫu dự kiến cộng hai lần watchdog và 5 giây. Có thể tăng
giới hạn này khi thử đường truyền rất chậm/mất gói nhiều. Sensor giữ tối đa một
mẫu đang chờ echo và ACK, nên chu kỳ là khoảng cách tối thiểu giữa các mẫu;
backpressure có thể làm tốc độ thực tế chậm hơn.

## 3. Thử độ trễ, băng thông và mất gói

```bash
sudo /usr/bin/python3 scripts/mininet-demo.py \
  --count 50 --interval-ms 200 --bw 10 --delay-ms 10 --loss 1
```

`--bw` tính bằng Mbps, `--delay-ms` là độ trễ một chiều và `--loss` là phần
trăm mất gói. Cấu hình áp dụng trên **cả hai link và cả hai chiều**: với
10 ms mỗi link, RTT nền xấp xỉ 40 ms. Mất gói trên cả hành trình có thể lớn
hơn giá trị của một link. Quan sát `srtt_ms`, `lost_packets` và việc các mẫu
vẫn được echo đúng. Kết quả của một lần thử chưa đủ để suy ra hiệu năng tổng
quát; thời gian xử lý của máy thật cũng ảnh hưởng.

## 4. Tự thao tác trong Mininet CLI

```bash
sudo /usr/bin/python3 scripts/mininet-demo.py --cli --timeout 120
```

Runner in sẵn lệnh server/client với đường dẫn tuyệt đối. Trong prompt
`mininet>`, lệnh bắt đầu bằng tên host sẽ chạy trong host đó:

```text
mininet> nodes
mininet> net
mininet> h1 ip addr show h1-eth0
mininet> h2 ip addr show h2-eth0
mininet> h1 ping -c 3 10.0.0.2
```

Chạy lệnh `h2 ...quic-server... &` mà runner đã in trước, rồi lệnh
`h1 ...quic-client...`. Server chỉ phục vụ một kết nối, nên phải khởi động
lại server trước mỗi lần chạy client mới. Dùng `h2 cat /tmp/quic-h2.log` để
xem log server.

Muốn sensor tiếp tục gửi cho đến khi dừng thủ công, mở CLI với:

```bash
sudo /usr/bin/python3 scripts/mininet-demo.py --cli --count 0 --timeout 120
```

Sau đó chạy hai lệnh đã in. Trong chế độ sensor, lần `Ctrl+C` đầu yêu cầu
client gửi FIN, chờ echo hoàn tất rồi đóng kết nối; lần thứ hai buộc dừng.
`--timeout` chỉ dừng khi không có tiến triển trong khoảng đó. Lượt tự động
yêu cầu số mẫu hữu hạn để có kết quả PASS/FAIL rõ ràng. Gõ `exit` để thoát
Mininet và dọn topology.

## 5. Khi không chạy được

- `Missing ...`: cài các gói ở bước 1; chạy `make` và `make certs` trước.
- `No module named mininet`: dùng `/usr/bin/python3` của hệ thống đã cài gói
  Mininet, tránh Python từ virtualenv không có module này.
- OVS không sẵn sàng: kiểm tra dịch vụ bằng `sudo ovs-vsctl show`.
- Báo bridge `s1` đã tồn tại: đóng lượt Mininet trước. Runner không xóa
  bridge đang tồn tại của một lượt khác.
- `Operation not permitted`: cần máy/VM cho phép network namespace, veth,
  Open vSwitch và traffic control. Root trong container bị giới hạn quyền
  vẫn có thể không đủ.
- Ping được nhưng TLS lỗi: xem log và SAN chứng chỉ, CA được client tin và
  `--server-name`; dùng cùng `server.crt` mà server thực sự đang dùng.

Nếu một lượt Mininet bị crash để lại interface/bridge, có thể dọn bằng
`sudo mn -c` **sau khi đã đóng tất cả lượt Mininet đang dùng**; lệnh này dọn
toàn cục nên runner không tự gọi nó.

Tài liệu chính thức:

- [Mininet Walkthrough](https://mininet.org/walkthrough/)
- [OVSBridge API](https://mininet.org/api/classmininet_1_1node_1_1OVSBridge.html)
- [Node API, gồm popen](https://mininet.org/api/classmininet_1_1node_1_1Node.html)
