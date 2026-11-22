# QUIC testbed: sensor liên tục, một client — một server

Đây là chương trình C chạy trên **Linux/WSL**, dùng **ngtcp2 1.25.0** và
**GnuTLS >= 3.7.2**. Server và client giao tiếp bằng QUIC v1 trên UDP, bắt tay
TLS 1.3 và trao đổi dữ liệu qua **một bidirectional stream, ID 0**.
Client gửi dữ liệu, server echo nguyên vẹn, client so sánh từng byte.
Mỗi lần chạy xử lý một kết nối. Client hỗ trợ gửi một message/file hoặc sensor
mô phỏng gửi JSONL định kỳ qua cùng stream cho đến khi đủ mẫu hay nhấn Ctrl+C.

## Chạy sensor trên Mininet h1–s1–h2

```text
h1 (sensor, 10.0.0.1) —— s1 (Open vSwitch bridge) —— h2 (server, 10.0.0.2)
```

Mininet chạy binary Linux thật trong các network namespace: hai host có IP
riêng nhưng dùng chung filesystem. QUIC chạy trên UDP nên không cần đổi sang
giao thức riêng cho Mininet. Cần Linux có quyền root và hỗ trợ namespace/veth.

Sau khi build và tạo cert bằng các lệnh ở mục 3:

```bash
sudo /usr/bin/python3 scripts/mininet-demo.py
```

Lệnh tạo topology, chạy server trên h2 và sensor trên h1 (20 mẫu, mỗi giây),
kiểm tra echo rồi dọn topology. Không cần controller SDN. Xem
[hướng dẫn Mininet từng bước](docs/MININET.md) để cài gói, tự dùng CLI,
gửi vô hạn hoặc thêm delay/loss/bandwidth.

Sensor cũng chạy thử được trên loopback, không cần sudo. Trong hai terminal
ở thư mục này, chạy server trước rồi chạy client:

```bash
./bin/quic-server --host 127.0.0.1 --timeout 30
./bin/quic-client --host 127.0.0.1 --sensor --interval-ms 1000 --count 0 --timeout 30
```

`--count 0` gửi đến khi nhấn Ctrl+C ở client; `--count 100` gửi đúng 100 mẫu.
Một mẫu gồm `seq`, `timestamp_ms` (Unix ms), `temperature_c`, `humidity_pct`.
Đây là dữ liệu giả lập. Client kiểm tra echo từng byte cho mỗi mẫu; lần Ctrl+C
đầu gửi FIN và chờ mẫu cuối hoàn tất, lần thứ hai buộc dừng.
Sensor giữ tối đa một mẫu đang chờ echo/ACK; chu kỳ là khoảng cách tối thiểu,
đường truyền chậm có thể giảm tốc độ lấy mẫu. Sensor không giới hạn tổng
stream ở 64 KiB; hàng đợi truyền lại vẫn có bộ nhớ giới hạn.

## 1. Vị trí và cấu trúc

Mã được triển khai tại:

```text
/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/demo/src
```

```text
src/
├── server/
│   ├── main.c
│   ├── server.c
│   ├── server.h
│   ├── connection.c
│   ├── connection.h
│   ├── tls.c
│   └── tls.h
├── client/
│   ├── main.c
│   ├── client.c
│   ├── client.h
│   ├── connection.c
│   ├── connection.h
│   ├── tls.c
│   └── tls.h
├── common/
│   ├── testbed.c
│   └── testbed.h
├── scripts/
│   ├── build-deps.sh
│   ├── gen-certs.sh
│   └── mininet-demo.py
├── docs/MININET.md
├── tests/integration.py
├── Makefile
├── README.md
├── backup/server.c.original   # bản phác thảo của bạn trước triển khai
├── certs/                    # chứng chỉ và khóa test cục bộ, sinh bằng make certs
├── .deps/                    # thư viện ngtcp2 riêng, sinh bằng make deps
├── build/                    # object files
└── bin/
    ├── quic-server
    └── quic-client
```

`main.c` đọc tham số; `server.c`/`client.c` điều phối socket và kết nối;
`connection.c` khởi tạo ngtcp2 theo từng vai trò; `tls.c` cấu hình GnuTLS.
`common/testbed.c` dùng chung callbacks, buffer stream, timer, I/O và thống kê.
Makefile chỉ biên dịch các file thuộc ba thư mục mã trên.

## 2. Chạy ngay trên máy hiện tại

Mở **hai terminal Linux**, chuyển vào thư mục:

```bash
cd /home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/demo/src
```

Terminal thứ nhất, chạy server trước:

```bash
./bin/quic-server --host 127.0.0.1 --port 4433 --timeout 120
```

Terminal thứ hai:

```bash
./bin/quic-client --host 127.0.0.1 --port 4433 --message "Xin chao QUIC"
```

Client phải in `handshake complete`, `ECHO VERIFIED`, `result=PASS`.
Server phải in `handshake complete`, số byte nhận được và `result=PASS`.
Chạy lại server trước mỗi lượt client tiếp theo. Sau khi build có
hai binary Linux trong `bin/` và thư viện trong `.deps/`; chạy từ thư mục `src`
để các đường dẫn chứng chỉ mặc định đúng.

## 3. Build lại hoặc dựng trên máy Linux mới

Ubuntu/Debian, cài các công cụ cần thiết:

```bash
sudo apt-get update
sudo apt-get install -y build-essential pkg-config cmake libgnutls28-dev \
    ca-certificates curl xz-utils openssl python3
```

Tại thư mục gốc của bộ mã:

```bash
make deps
make -j4
make certs
make test
```

`make deps` tải release ngtcp2 **1.25.0** từ GitHub chính thức, đối chiếu SHA256,
build backend GnuTLS và cài vào `.deps/` của bộ mã. Không cài ngtcp2 vào thư mục
hệ thống. GnuTLS development và compiler phải được cài bằng package manager.
Nếu máy đã có ngtcp2 >= 1.25.0 cùng backend GnuTLS, có thể thử `make` trực tiếp.

Thư viện `.deps/lib` được tìm tương đối với binary bằng ELF RUNPATH. Nếu dùng
`DEPS_PREFIX` khác mặc định, cần thêm thư mục `lib` của prefix đó vào
`LD_LIBRARY_PATH` khi chạy. Gói `.deps` được tạo từ source này chỉ chứa thư viện
ngtcp2; thư viện GnuTLS runtime vẫn do Linux cung cấp.

`make clean` xóa object và hai executable, giữ nguyên dependency, chứng chỉ và
bản sao lưu mã cũ. `make certs` giữ chứng chỉ đã tồn tại; không thay chứng chỉ
trong thư mục `QUICSocket/certs` cũ của dự án.

## 4. Đưa lên hai máy trong testbed

Ví dụ server có IPv4 `192.168.1.20`.

1. Chép `server/`, `client/`, `common/`, `scripts/`, `tests/`, `Makefile` và README
   sang Linux trên từng máy. Chạy `make deps` và `make -j4` tại mỗi máy để tạo
   binary đúng kiến trúc CPU. Có thể dùng `make bin/quic-server` hoặc
   `make bin/quic-client` để chỉ build vai trò cần triển khai.
2. Trên server, tạo chứng chỉ có SAN chứa đúng IP server:

   ```bash
   SERVER_IP=192.168.1.20 make certs
   ```

   Lệnh chỉ tạo khi chưa có cặp `certs/server.crt` và `certs/server.key`.
   Nếu đang dùng chứng chỉ cũ, cấp một cặp mới ở vị trí riêng rồi chỉ định
   `--cert`/`--key`; không kỳ vọng `make certs` sửa SAN của chứng chỉ hiện có.
3. Chép **chỉ `server.crt` của server** sang client, chẳng hạn
   `certs/server.crt`. Client phải tin đúng chứng chỉ mà server đang dùng;
   không sinh một chứng chỉ độc lập trên client rồi dùng làm CA cho server.
   Khóa riêng `server.key` chỉ cần trên server.
4. Server lắng nghe trên các interface IPv4:

   ```bash
   ./bin/quic-server --host 0.0.0.0 --port 4433 --timeout 120
   ```

5. Client kết nối tới IP thật của server:

   ```bash
   ./bin/quic-client --host 192.168.1.20 --port 4433 \
       --ca certs/server.crt --message "Hello from client 1"
   ```

Hai máy phải có đường truyền tới nhau và UDP port 4433 được cho phép qua
firewall. Nếu server dùng WSL, địa chỉ truy cập từ LAN phụ thuộc chế độ mạng
WSL/Windows; lần chạy loopback trên WSL chỉ kiểm tra giao tiếp trong cùng WSL.

Nếu chứng chỉ chỉ chứa `DNS:localhost`, có thể dùng
`--server-name localhost --ca certs/server.crt` để kiểm tra đúng danh tính đó
qua IP testbed; client vẫn xác thực chứng chỉ. Cách trực tiếp cho testbed là
cấp chứng chỉ có SAN IP thật rồi dùng IP làm danh tính mặc định.

## 5. Gửi file nhị phân

Khởi động lại server, sau đó:

```bash
./bin/quic-client --host 127.0.0.1 --file payload.bin --output echoed.bin
cmp payload.bin echoed.bin
```

Giới hạn mỗi lượt là **65536 byte**; hỗ trợ file rỗng và byte NUL.
`--message` và `--file` loại trừ nhau. Chỉ ghi `--output` sau khi echo đã được
xác minh và kết nối hoàn tất. Chỉ định một file output khác file input.

## 6. Luồng giao thức và giới hạn

```text
Client                                Server
  |--- QUIC Initial / TLS ClientHello -->|
  |<------ TLS 1.3 handshake ------------|
  |------- TLS handshake completion ---->|
  |--- STREAM 0: request/sample --------->|
  |<-- STREAM 0: identical echo ---------|
  |--- ... các mẫu tiếp theo ... -------->|
  |--- STREAM 0: FIN khi hết dữ liệu ---->|
  |<-- STREAM 0: echo còn lại + FIN ------|
  |--- ACK / application CLOSE(0) ------->|
```

Callbacks crypto của ngtcp2 đưa CRYPTO frame vào GnuTLS. Client chỉ mở stream
sau handshake, kiểm tra độ dài và từng byte echo trước khi báo thành công.
ALPN của hai phía là `quic-testbed/1`.

Vòng lặp dùng socket nonblocking và `poll`, kết hợp
`ngtcp2_conn_get_expiry`/`ngtcp2_conn_handle_expiry` để xử lý ACK, PTO và truyền lại.
Buffer gửi có địa chỉ cố định, chỉ tái sử dụng sau ACK để ngtcp2 retransmit.
Client sensor còn chờ so sánh echo trước khi tái sử dụng. Server echo ngay
mỗi chunk, giữ tối đa 64 KiB chờ ACK và cấp thêm flow-control credit khi
echo đã ACK; stream dài không làm bộ nhớ tăng theo thời gian.
Không tắt kiểm tra chứng chỉ. Stream rỗng vẫn gửi FIN.

Server bỏ qua datagram không phải Initial QUIC v1 trước khi chọn peer.
Khi chọn được peer, UDP socket được connect để chỉ nhận một client.
Khi client đóng với application code 0 sau khi đã nhận toàn bộ echo, server
cũng chấp nhận hoàn tất dù delayed ACK cho FIN chưa tới; ngtcp2 không gọi
stream-close callback cho stream còn tồn tại khi toàn kết nối bị đóng.

Đây là nền tảng thử nghiệm một kết nối, một stream, IPv4; chưa triển khai
HTTP/3, nhiều client, nhiều stream ứng dụng, 0-RTT, Retry phía server hay
connection migration. Chứng chỉ tự ký dành cho testbed.

## 7. Log và kiểm thử

```bash
./bin/quic-server --timeout 120 --verbose
./bin/quic-client --message "Hello QUIC" --verbose
make test
```

Tham số chung: `--host`, `--port`, `--timeout` (1..3600 giây), `--verbose`, `--help`.
Server thêm `--cert`, `--key`. Client thêm `--ca`, `--server-name`, `--message`,
`--file`, `--output`, `--sensor`, `--interval-ms` (1..60000), `--count`
(0..1000000). Sensor không dùng cùng message/file/output. Interval và count
chỉ dùng với sensor; interval phải nhỏ hơn timeout tính theo ms.
Port hợp lệ là 1..65535. `--host` nhận IPv4 dạng số;
client không nhận `0.0.0.0` làm đích.

Timeout mặc định 15 giây. Server có tối đa một khoảng timeout chờ Initial,
rồi tính timeout không có tiến triển: cập nhật khi handshake, nhận dữ liệu
hoặc ACK echo. Client một lượt tính tổng timeout từ lúc khởi tạo; client
sensor dùng timeout không có tiến triển, cập nhật khi handshake/nhận echo.
Vì vậy sensor có thể chạy lâu hơn timeout nếu vẫn trao đổi dữ liệu.
Ctrl+C dừng chương trình và giải phóng tài nguyên; sensor drain như mô tả trên.
Mã thoát 0 là thành công/help, 1 là lỗi chạy, 2 là lỗi tham số.

Log có số byte ứng dụng đã nộp cho QUIC/đã nhận, số datagram UDP và byte UDP,
`handshake_ms`, `srtt_ms`, `cwnd`, số packet bị ngtcp2 xem là mất, cùng
`echo_ms` phía client. Thời gian handshake client có cả thời gian thiết lập
cục bộ; server đo từ Initial được chấp nhận. `echo_ms` đo từ lúc mở stream tới
lúc nhận và xác minh FIN của echo. Các số này hỗ trợ quan sát, chưa phải bộ
benchmark throughput. `wire_tx/rx` tính payload UDP, không tính header IP/UDP.

Bộ kiểm thử chạy các tiến trình thật, kiểm tra: thông điệp, datagram rác trước
Initial, stream rỗng, file nhị phân 64 KiB, truyền lại khi relay làm mất Initial
ở cả hai chiều, giới hạn kích thước, CA sai, danh tính chứng chỉ sai, timeout
hai phía và tham số sai. Còn có sensor định kỳ, stream sensor vượt 64 KiB,
stream sống quá inactivity timeout khi đang hoạt động, mất Initial và dừng
sensor vô hạn bằng SIGINT. Không cần server thủ công khi `make test`.
`make test` dùng UDP loopback; topology thật chạy riêng bằng script Mininet.

Nguồn API chính thức:

- [ngtcp2 programmers' guide](https://nghttp2.org/ngtcp2/programmers-guide.html)
- [ngtcp2 release v1.25.0](https://github.com/ngtcp2/ngtcp2/releases/tag/v1.25.0)
- [GnuTLS: certificate verification](https://www.gnutls.org/manual/html_node/Certificate-verification.html)
