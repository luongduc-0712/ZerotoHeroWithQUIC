# ZerotoHeroWithQUIC

> Research, socket programming implementations, and performance benchmarks for the QUIC protocol.

---

# 1. Đặt vấn đề

Giả sử chúng ta có máy tính **A** và **B** kết nối mạng LAN, và chúng ta muốn gửi dữ liệu từ máy A sang máy B, với điều kiện chúng ta đã biết địa chỉ IP của cả hai máy và hai máy đã kết nối thành công.

Bây giờ từ máy A, ta muốn chào hỏi máy B với nội dung:

```text
Hello
```

Vậy làm cách nào để máy B nhận được dữ liệu?

Dữ liệu nằm ở **tầng ứng dụng (user space)**, chúng ta cần gửi vào **kernel space**, sau đó kernel từ máy A truyền dữ liệu đến kernel máy B.

Nhưng làm sao để truyền dữ liệu từ user space xuống kernel space và ngược lại? Trong kernel space dữ liệu sẽ được xử lý như thế nào?

Đó là hai vấn đề cần giải đáp:

1. **Dữ liệu được đưa từ user space xuống kernel space như thế nào?**
2. **Trong kernel space dữ liệu được xử lý và truyền từ máy A sang máy B như thế nào?**

---

# 2. Các khái niệm cơ bản

Các khái niệm và cách các giao thức hoạt động cơ bản cần hiểu:

- IP
- Port
- Protocol
- Server - Client
- TCP
- UDP

---

## 2.1. IP - địa chỉ

IP có thể được hình dung giống như **địa chỉ nhà**.

Khi bên A biết địa chỉ của B thì A có thể xác định dữ liệu cần được gửi tới máy B.

Tuy nhiên, giả sử máy B có nhiều chương trình đang chạy, vậy làm thế nào để dữ liệu được gửi đến **đúng chương trình cần dữ liệu đó**?

=> Cần sử dụng **Port**.

---

## 2.2. Port

Port là một số được sử dụng ở **tầng Transport**, giúp phân biệt các điểm giao tiếp.

Để kết nối từ máy A đến máy B, chúng ta cần biết:

- địa chỉ IP của máy B;
- Port mà chương trình trên máy B sử dụng.

Ví dụ:

```text
192.168.1.20:9090
```

Trong đó:

```text
192.168.1.20 -> IP
9090         -> Port
```

---

# 3. TCP

TCP có các đặc điểm cần làm rõ:

- **Thiết lập kết nối**: cần làm rõ.
- **Cung cấp byte stream**: cần làm rõ.
- **Đảm bảo dữ liệu giao cho ứng dụng một cách tuần tự, đúng dữ liệu**: cần làm rõ.
- **Có cơ chế truyền lại khi cần**: cần làm rõ.

---

# 4. UDP

UDP có các đặc điểm cần làm rõ:

- **Không cần thiết lập kết nối**: cần làm rõ.
- **Truyền theo datagram**: cần làm rõ.
- **Không đảm bảo dữ liệu đến đích hay đến đúng thứ tự**: cần làm rõ.
- **Không cung cấp cơ chế truyền lại đáng tin cậy như TCP**: cần làm rõ.

---

# 5. Client và Server

## Server

Server là chương trình cung cấp dịch vụ và chờ Client kết nối.

## Client

Client là chương trình chủ động kết nối với Server.

Cần làm rõ thêm:

- Server thực sự chờ ở đâu?
- Client kết nối tới cái gì?
- Một Server có thể phục vụ nhiều Client như thế nào?
- Listening socket và connected socket có gì khác nhau?

---

# 6. Luồng dữ liệu tổng quát

Tóm lại, dữ liệu sẽ đi như sau:

```text
Chương trình trên Client
        |
        v
    Socket API
        |
        v
Giao thức truyền tải
        |
        v
 Kết nối giữa 2 máy
        |
        v
Kernel trên máy Server
        |
        v
    Socket API
        |
        v
Chương trình trên Server
```

Hay tổng quát hơn:

```text
User space A
    |
    v
Socket API
    |
    v
Kernel A
    |
    v
Network
    |
    v
Kernel B
    |
    v
Socket API
    |
    v
User space B
```

---

# 7. Vấn đề 1: Dữ liệu được đưa xuống kernel space như thế nào?

## Cách xử lý

Ta cần một giao diện để chương trình có thể gửi dữ liệu xuống kernel.

Giao diện đó là:

```text
Socket API
```

---

# 8. Socket, Socket API và Socket Programming

## 8.1. Socket

Socket là một **điểm cuối giao tiếp**.

Có thể hình dung socket giống như một điểm giao nhận dữ liệu giữa chương trình và hệ điều hành.

Socket là một tài nguyên do kernel tạo và quản lý.

---

## 8.2. Socket API

Socket API là **giao diện** gồm các hàm mà chương trình sử dụng để thao tác với socket.

Ví dụ:

```c
socket();
bind();
listen();
accept();
connect();
send();
recv();
close();
```

Không nên hiểu socket là nơi “chứa” Socket API.

Phân biệt:

```text
Socket
-> tài nguyên / điểm giao tiếp do kernel quản lý

Socket API
-> tập hợp các hàm để chương trình thao tác với socket
```

---

## 8.3. Socket Programming

Socket Programming là công việc lập trình sử dụng Socket API để xây dựng chương trình giao tiếp mạng.

Ví dụ:

```text
TCP Server
TCP Client
UDP Server
UDP Client
```

Tóm lại:

> Chương trình giao tiếp với hệ điều hành qua Socket API.

---

# 9. Các hàm Socket API cơ bản

Các hàm chúng ta sẽ sử dụng:

```c
socket();   // tạo socket
bind();     // gắn socket với địa chỉ cục bộ
listen();   // chuẩn bị tiếp nhận kết nối
accept();   // lấy một kết nối đã thiết lập
connect();  // chủ động kết nối
send();     // gửi dữ liệu
recv();     // nhận dữ liệu
close();    // đóng file descriptor
```

---

# 10. Các thư viện cơ bản

## `<sys/socket.h>`

Chứa:

```c
socket()
bind()
listen()
accept()
connect()
send()
recv()

struct sockaddr
socklen_t
```

## `<netinet/in.h>`

Chứa:

```c
struct sockaddr_in
AF_INET
AF_INET6
INADDR_ANY
htons()
ntohs()
htonl()
ntohl()
```

## `<arpa/inet.h>`

Chứa:

```c
inet_pton()
inet_ntop()
```

`inet_ntop()` dùng để chuyển địa chỉ IP dạng nhị phân thành chuỗi dễ đọc.

## `<unistd.h>`

Chứa:

```c
close()
```

## `<stdio.h>`

Chứa:

```c
printf()
perror()
getchar()
```

---

# 11. File Descriptor

## 11.1. Khái niệm

File Descriptor là một **số nguyên không âm** mà tiến trình sử dụng để tham chiếu đến một tài nguyên I/O.

Ví dụ, một chương trình sử dụng hai tài nguyên I/O khác nhau:

- mở file;
- tạo socket.

Khi đó chương trình sử dụng FD để tham chiếu đến tài nguyên cần thao tác.

Ví dụ:

```c
read(fd, buf, 100);
```

Có thể hiểu là:

> Chương trình yêu cầu đọc tối đa 100 byte từ tài nguyên được tham chiếu bởi `fd`, sau đó ghi dữ liệu vào bộ nhớ đệm `buf`.

---

## 11.2. File Descriptor và Process

Khi chạy program, kernel tạo **process** để thực thi chương trình.

Mỗi process có một bảng File Descriptor riêng.

Ví dụ:

```text
Process A

FD 0 -> stdin
FD 1 -> stdout
FD 2 -> stderr
FD 3 -> socket
```

Một process khác cũng có thể có:

```text
Process B

FD 0 -> stdin
FD 1 -> stdout
FD 2 -> stderr
FD 3 -> một tài nguyên khác
```

Như vậy:

- Trong **cùng một process**, hai tài nguyên đang mở không sử dụng cùng một FD tại cùng thời điểm.
- Trong **hai process khác nhau**, FD có thể trùng giá trị vì chúng thuộc hai bảng FD khác nhau.

Kernel phân biệt các process thông qua **Process ID (PID)**.

---

## 11.3. Kiểm tra PID và FD trên Linux

Tìm PID:

```bash
pgrep -a tên_chương_trình
```

Ví dụ:

```bash
pgrep -a tcp_server
```

Xem các File Descriptor của process:

```bash
ls -l /proc/PID/fd
```

Ví dụ:

```bash
ls -l /proc/12345/fd
```

---

# 12. Hàm `socket()`

## 12.1. Mục đích

Hàm `socket()` dùng để:

> Tạo socket và trả về File Descriptor dùng để tham chiếu đến socket đó.

Hàm được khai báo trong:

```c
#include <sys/socket.h>
```

Nguyên mẫu:

```c
int socket(int domain, int type, int protocol);
```

---

## 12.2. Tham số `domain`

`domain` xác định họ địa chỉ mà socket sử dụng.

### `AF_INET`

```c
AF_INET
```

Xác định socket sử dụng IPv4.

### `AF_INET6`

```c
AF_INET6
```

Xác định socket sử dụng IPv6.

### `AF_UNIX`

```c
AF_UNIX
```

Dùng để giao tiếp nội bộ trên cùng một máy.

---

## 12.3. Tham số `type`

### `SOCK_STREAM`

```c
SOCK_STREAM
```

Luồng byte, thường sử dụng với TCP.

### `SOCK_DGRAM`

```c
SOCK_DGRAM
```

Datagram, thường sử dụng với UDP.

---

## 12.4. Tham số `protocol`

Tham số `protocol` xác định giao thức cần sử dụng.

Có thể đặt:

```c
0
```

Khi đó kernel tự chọn giao thức phù hợp với `domain` và `type`.

Hoặc có thể chỉ rõ:

```c
IPPROTO_TCP
```

hoặc:

```c
IPPROTO_UDP
```

Ví dụ:

```c
socket(AF_INET, SOCK_STREAM, 0);
```

hoặc:

```c
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
```

### Kết luận

Các tham số:

```text
domain
type
protocol
```

mô tả **socket mà chúng ta muốn tạo**.

Sau đó kernel tạo socket tương ứng và trả về một File Descriptor cho process.

---

# 13. Kernel quản lý File Descriptor

Với mỗi process sẽ có một bảng tham chiếu riêng.

Ví dụ:

```text
Process A
PID = 1000

FD 3
 |
 v
Socket A
```

và:

```text
Process B
PID = 2000

FD 3
 |
 v
Socket B
```

Hai process đều có thể có FD bằng `3`, nhưng chúng thuộc hai bảng FD khác nhau.

Kernel biết chương trình đang thao tác trong context của process nào, vì vậy kernel xác định được chính xác FD đó đang tham chiếu tới tài nguyên nào.

---

# 14. Hàm `bind()`

Giả sử máy A có `IP1` và nhiều Port khác nhau.

Chúng ta muốn dùng `Port1` để giao tiếp với máy khác.

Khi đó chúng ta cần **gắn địa chỉ IP và Port của máy A cho socket đang chạy trên máy A**.

Đó là nhiệm vụ của:

```c
bind()
```

---

## 14.1. Địa chỉ cục bộ là gì?

Địa chỉ cục bộ là:

```text
IP của chính máy đang chạy chương trình
+
Port mà chương trình muốn sử dụng
```

Ví dụ:

```text
IP   = 192.168.1.10
Port = 9090
```

Địa chỉ cục bộ:

```text
192.168.1.10:9090
```

---

## 14.2. Cấu trúc hàm `bind()`

```c
int bind(
    int sockfd,
    const struct sockaddr *addr,
    socklen_t addrlen
);
```

Trong đó:

```text
sockfd
-> FD của socket cần gắn địa chỉ

addr
-> địa chỉ vùng nhớ chứa thông tin địa chỉ cục bộ

addrlen
-> kích thước vùng nhớ chứa cấu trúc địa chỉ
```

---

# 15. `struct sockaddr_in`

Với IPv4, chúng ta sử dụng:

```c
struct sockaddr_in
```

Cấu trúc này có các trường quan trọng:

```text
sin_family -> họ địa chỉ IPv4
sin_port   -> Port cục bộ
sin_addr   -> địa chỉ IP cục bộ
```

Ví dụ:

```c
struct sockaddr_in server_addr = {0};

server_addr.sin_family = AF_INET;
server_addr.sin_port = htons(9090);
server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
```

---

# 16. Network Byte Order

Port là một số nguyên 16 bit.

Trong máy tính có hai cách sắp xếp byte phổ biến:

```text
Big-endian
Little-endian
```

Trong network, quy ước sử dụng:

```text
Big-endian
```

Vì vậy khi đưa giá trị từ host vào cấu trúc dùng cho network, cần chuyển đổi về network byte order.

---

## 16.1. `htons()`

```c
htons()
```

Dùng với giá trị 16 bit, ví dụ Port.

```c
server_addr.sin_port = htons(9090);
```

Có thể nhớ:

```text
h -> host
to
n -> network
s -> short
```

---

## 16.2. `htonl()`

```c
htonl()
```

Dùng với giá trị 32 bit.

Ví dụ:

```c
server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
```

Có thể nhớ:

```text
h -> host
to
n -> network
l -> long
```

---

# 17. Hàm `listen()`

Hàm `listen()` nằm trong:

```c
#include <sys/socket.h>
```

Nguyên mẫu:

```c
int listen(int sockfd, int backlog);
```

Khi Server gọi:

```c
listen()
```

Server yêu cầu kernel:

1. Chuyển socket sang trạng thái lắng nghe kết nối TCP.
2. Thiết lập giới hạn hàng đợi kết nối.

---

## 17.1. Điều gì xảy ra khi Client gọi `connect()`?

Khi Client gọi:

```c
connect()
```

kernel bên Client gửi `SYN` đến Server.

Sau đó:

```text
Client kernel                  Server kernel

     SYN
      ----------------------->

                  SYN + ACK
      <-----------------------

     ACK
      ----------------------->
```

Sau khi handshake hoàn thành, kernel phía Server đã có một kết nối TCP được thiết lập.

Tuy nhiên, chương trình Server chưa nhất thiết đã gọi:

```c
accept()
```

để lấy kết nối đó ra xử lý.

Client có thể gửi dữ liệu sau khi kết nối TCP được thiết lập. Kernel phía Server có thể nhận dữ liệu và giữ trong buffer trước khi chương trình Server gọi `accept()` để tiếp nhận connected socket.

---

## 17.2. `backlog`

Tham số:

```c
backlog
```

xác định giới hạn hàng đợi các kết nối TCP đã thiết lập thành công nhưng chưa được chương trình Server tiếp nhận bằng:

```c
accept()
```

Ví dụ:

```c
listen(server_fd, 10);
```

---

# 18. Hàm `accept()`

Hàm `accept()` được khai báo trong:

```c
#include <sys/socket.h>
```

Nguyên mẫu:

```c
int accept(
    int sockfd,
    struct sockaddr *addr,
    socklen_t *addrlen
);
```

---

## 18.1. Mục đích của `accept()`

Khi chương trình Server gọi:

```c
accept()
```

chương trình yêu cầu kernel lấy một kết nối đã được thiết lập trên listening socket.

Nếu thành công, `accept()` trả về File Descriptor của một **connected socket** mới.

Ví dụ:

```text
server_fd = 3
client_fd = 4
```

Trong đó:

```text
server_fd
-> listening socket

client_fd
-> connected socket dùng để trao đổi dữ liệu với Client
```

---

## 18.2. Kernel đã biết thông tin Client từ đâu?

Trước khi chương trình gọi `accept()`, kernel đã tham gia quá trình TCP handshake.

Do đó kernel đã có các thông tin như:

```text
Client IP
Client Port
```

Khi gọi `accept()`, chương trình có thể yêu cầu kernel sao chép các thông tin này ra vùng nhớ của Server.

---

## 18.3. Tham số `addr`

```c
struct sockaddr *addr
```

Tham số `addr` sử dụng **con trỏ** để kernel có thể ghi thông tin địa chỉ Client vào vùng nhớ mà chương trình đã chuẩn bị.

Ví dụ chương trình tạo:

```c
struct sockaddr_in client_addr;
```

Sau đó truyền:

```c
(struct sockaddr *)&client_addr
```

vào `accept()`.

Nếu không sử dụng con trỏ thì kernel không thể ghi IP và Port của Client vào chính vùng nhớ mà chương trình Server đã chuẩn bị.

---

## 18.4. Tham số `addrlen`

```c
socklen_t *addrlen
```

`addrlen` cũng sử dụng con trỏ vì kernel cần thực hiện hai việc:

### Trước khi `accept()` chạy

Kernel cần đọc kích thước vùng nhớ mà chương trình cung cấp.

Ví dụ:

```c
socklen_t client_len = sizeof(client_addr);
```

### Sau khi `accept()` chạy

Kernel cập nhật lại biến này bằng kích thước địa chỉ thực tế đã được ghi.

Vì vậy phải truyền:

```c
&client_len
```

Ví dụ:

```c
int client_fd = accept(
    server_fd,
    (struct sockaddr *)&client_addr,
    &client_len
);
```

---

# 19. Tiến trình TCP Server hiện tại

Từ những kiến thức đã học, có thể hình dung TCP Server theo trình tự:

```text
socket()
    |
    v
Tạo socket + FD

bind()
    |
    v
Gắn socket với IP + Port cục bộ

listen()
    |
    v
Chuyển socket thành listening socket

accept()
    |
    v
Lấy một kết nối TCP đã được thiết lập
và nhận connected socket FD

recv() / send()
    |
    v
Nhận và gửi dữ liệu

close()
    |
    v
Đóng FD
```

---

# 20. TCP Client hiện tại

Phía Client có thể hình dung:

```text
socket()
    |
    v
Tạo socket

connect()
    |
    v
Chủ động yêu cầu thiết lập kết nối TCP tới Server

send() / recv()
    |
    v
Gửi và nhận dữ liệu

close()
    |
    v
Đóng socket
```

---

# 21. Các phần cần tiếp tục làm rõ

## TCP

- Thiết lập kết nối thực sự nghĩa là gì?
- `connect()` làm gì với socket?
- Byte stream là gì?
- `send()` thực sự đưa dữ liệu đi đâu?
- `recv()` thực sự lấy dữ liệu từ đâu?
- TCP đảm bảo thứ tự như thế nào?
- TCP truyền lại dữ liệu như thế nào?
- Tại sao một `send()` không tương ứng với một `recv()`?

## UDP

- Không thiết lập kết nối nghĩa là gì?
- Datagram là gì?
- `sendto()` và `recvfrom()` hoạt động như thế nào?
- UDP socket khác TCP socket ở đâu?

## QUIC

Sau khi hiểu TCP và UDP socket programming:

```text
Application
    |
    v
QUIC
    |
    v
UDP
    |
    v
IP
```

Cần tiếp tục tìm hiểu:

- QUIC sử dụng UDP socket như thế nào?
- QUIC connection được tạo ra như thế nào?
- QUIC handshake hoạt động thế nào?
- QUIC stream là gì?
- QUIC tự xử lý ACK, retransmission và congestion control như thế nào?

# 22. QUIC
UDP cung cấp cho QUIC:
- port nguồn
- port đích
- checksum
- truyền datagram
- giao diện socket trên hệ điều hành

Còn lại QUIC sẽ xử lý phía trên, bao gồm:
- Connection
- Stream
- Reliability
- ACK
- Retransmission
- Congestion Control
- TLS encryption
## 1. Cơ chế hoạt động
QUIC lấy dữ liệu từ Applcation, QUIC tổ chức dữ liệu thành stream (giữ lại trình tự cho dữ liệu).
```text
=> Câu hỏi: QUIC tổ chức dữ liệu thành stream bằng cách nào?
```
Sau đó, Stream dât được đặt vào Frame nhằm mô tả được đây là dữ liệu của Stream nào, nằm ở vị trí nào trong Stream.
```text
Đơn giản hóa
    Stream ID 
    Offset
    Length
    Data
```
Frame được đặt vào QUIC packet
```text
QUIC Packet
┌─────────────────────────────┐
│ Header                      │
│                             │
│ Connection ID = ABC         │
│ Packet Number = 10          │
├─────────────────────────────┤
│ Payload                     │
│                             │
│ ┌─────────────────────────┐ │
│ │ STREAM Frame            │ │
│ │ Stream ID = 0           │ │
│ │ Offset = 0              │ │
│ │ Data = "Hello"          │ │
│ └─────────────────────────┘ │
└─────────────────────────────┘
```
## 2. ACK
ACK là gói được gửi như dữ liệu từ Server nhằm giúp cho Client nhận biết gói tin đã được gửi, trong trường hợp không nhận được gói ACK, Client không trực tiếp gửi lại ngay mà còn dựa vào Packiet Number + ACK + RTT + timer + Loss Detection
Có 2 cơ chế xác định gói ACK có mất hay không
- Packet Threshold: hiểu đơn giản là các packet có Packet Number lớn hơn nó đã được ACK đủ xa, QUIC sẽ coi là lost packet đó
- Time Threshold: Trong trường hợp đằng packet không nhận được gói ACK là quá ít hoặc không còn packet, trong lúc này áp dụng phương pháp: nếu packet đã chờ lâu hơn đáng kể so với thời gian mạng bình thường, nó có khả năng đã mất
- Khi phát hiện packet mất, QUIC sẽ lấy STREAM data chưa được ACK đưa vào một packet mới
```text
                 SEND PACKET
                      │
                      ↓
               lưu PN + sent_time
                      │
                      ↓
                  chờ ACK
                      │
          ┌───────────┴────────────┐
          │                        │
       ACK tới                chưa có ACK
          │                        │
          ↓                        ↓
   parse ACK ranges           PTO timer
          │                        │
          ↓                        ↓
  mark packet ACKED          timer expired?
          │                        │
          ↓                        ↓
     update RTT                  send probe
          │
          ↓
 kiểm tra các packet
 chưa được ACK
          │
     ┌────┴────┐
     │         │
packet      time
threshold   threshold
     │         │
     └────┬────┘
          ↓
       LOST?
          │
          ↓
        YES
          │
          ↓
 recovery frame/data
          │
          ↓
 packet mới PN mới
```
## 3. Tổng quan
```text
                    APPLICATION
                         │
                         ↓
                      STREAM
                         │
                         ↓
                       FRAME
                         │
                         ↓
                    QUIC PACKET
                    /          \
                  CID           PN
                   │             │
                   │             ↓
                   │            ACK
                   │             │
                   │             ↓
                   │            RTT
                   │             │
                   │             ↓
                   │      LOSS DETECTION
                   │             │
                   │             ↓
                   │         RECOVERY
                   │
                   ↓
          CONNECTION MANAGEMENT

                         │
                         ↓
                        UDP
                         ↓
                         IP
```
## 4. Lộ trình thực hiện
Xây dựng một QUIC v1 tối giản dựa trên các tài liệu chính:
- RFC 9000: QUIC Transport
- RFC 9001: QUIC TLS
- RFC 9002: Loss Detection & Congestion Control
### Các bước thực hiện:

| Giai đoạn | Nhiệm vụ chính | Nội dung thực hành | Kết quả |
|---|---|---|---|
| 1 | QUIC Packet | Tạo, Serialize, parse các QUIC packet |
| 2 | QUIC Frame | Implement PING, ACK, CRYPTO, STREAM, CONNECTION_CLOSE| 
| 3 | Connection Management | Connection ID, trạng thái, packet number space, connection timeout| 
| 4 | TLS 1.3 Integration | Tích hợp thư viện TLS, QUIC CRYPTO frame, packet protection|
| 5 | QUIC Handshake | Thiết lập kết nối Client - Server thực tế
| 6 | QUIC Stream | Gửi, nhận dữ liệu, quản lý stram và flow control| 
| 7 | ACK & Loss Recovery | RTT, ACK ranges, loss detection, PTO, retransmission|
| 8 | congestion Control | Congestion windown, slow strat, congestion advoidance|
| 9 | Testing | Kiểm thử truyền dữ liệu, mất gói và khả năng tương thích|

### Cách sắp xếp của QUIC
```text
UDP Datagram
│
└── QUIC Packet
    │
    ├── QUIC Header
    │
    └── Protected Payload
        │
        ├── STREAM Frame
        │   ├── Frame Type
        │   ├── Stream ID
        │   ├── Offset (nếu có)
        │   ├── Length (nếu có)
        │   └── Data: "Hello"
        │
        └── ...
```
```text
Ứng dụng (examples/client.c, server.c)
        │  dùng API công khai
        ▼
   quic.h                  ← "cửa ngõ" của thư viện
        │
        ▼
   quic_connection         ← quản lý một kết nối (trạng thái, Connection ID)
        │
   ┌────┼──────────────┬──────────────┐
   ▼    ▼              ▼              ▼
quic_stream     quic_recovery   quic_tls / quic_crypto
(luồng dữ liệu) (phát hiện mất  (bắt tay, mã hóa)
                 gói, gửi lại)
                     │
                     ▼
              quic_congestion  (điều tiết tốc độ gửi)
        │
        ▼
   quic_frame              ← các "thông điệp" nằm trong packet
        │
        ▼
   quic_packet             ← đóng gói, đánh số packet
        │
        ▼
   quic_varint             ← công cụ nền: mã hóa số nguyên
        │
        ▼
      UDP socket
```
## Phân biệt: QUIC Stream, QUIC Frame, QUIC Packet.
### QUIC Stream: Luồng dữ liệu ứng dụng. 
- Một connection có thể có nhiều Stream.
- STREAM thường cung cấp truyền dữ liệu tin cậy
- Dữ liệu được đưa tới dứng dụng theo đúng thứ tự trong Stream
- Mỗi STREAM đều có không gian vị trí byte (offset) riêng
- Các STREAM có thể hoạt động tương đối độc lập.
- Có 2 loại STREAM:
    - Bidirectional Stream: Cả Client và Server đều có thể gửi dữ liệu trên cùng 1 Stream.
    - Unidirectional Stream: Chỉ một phía gửi dữ liệu trên Stream đó.
- Tuy nhiên, QUIC Stream vẫn là luồng byte nên không tự bảo toàn ranh giới thông điệp. 
### QUIC Frame: Đơn vị dữ liệu bên trong QUIC Packet
- QUIC cần biểu diễn dữ liệu trong STREAM dưới dạng có cấu trúc để phía nhận có thể phân biệt bao gồm: dữ liệu thuộc Stream nào, nằm ở vị trí byte nào trong Stream, có bao nhiê byte, Stream đã kết thúc chưa?
- Một Stream có cấu trúc khái quát:
```Text
+---------------------+
|Frame Type           |
+---------------------+
|Stream ID            |
+---------------------+
|Offset (option)      |
+---------------------+
|Length (option)      |
+---------------------+
|Stream Data          |
+---------------------+
```
- Ngoài STREAM Frame, chúng ta còn:
    - ACK: Xác nhận những QUIC Packet đã nhận
    - CRYPRO: Mang dữ liệu phục vụ TLS Handshake
    - PING: Tạo gói cần đuọc xác nhận, ví dụ kiểm tra đường truyền
    - MAX_DATA: Thông báo giới hạn dữ liệu được phép gửi trên kết nối
    - MAX_STREAM_DATA: Thông báo giới hạn dữ liệu trên một Stream
    - CONNECTION_CLOSE: Thông báo đóng kết nối
    - PADDING: Bổ sung byte đệm
### QUIC Packet: Gói tin của QUIC
Sau khi tạo STREAM Frame, QUIC cần gửi Frame đó qua mạng. Nó sẽ đặt Frame vào một QUIC Packet với cấu trúc khai quát:
```Text
Một Packet có thể có nhiều Frame.
+-----------------------+
|     QUIC Header       |
+-----------------------+
|     QUIC Payload      |
|   +----------------+  |
|   |Stream Frame    |  |
|   +----------------+  |
|   |ACK Frame       |  |
|   +----------------+  |
|   |PADDING Frame   |  |
|   +----------------+  |
|                       |
+-----------------------+
```
- QUIC Header có thể chứa:
    - **Destination Connection ID (DCID):** Định danh kết nối ở phía nhận
    - **Source Connection ID (SCID):** Định danh kết nối ở phía gửi, đối với những Packet có trường này
    - **Packet Number:** Số thứ tự gói tin trong không gian tương ứng
    - **Packet Type:** Loại packet được thể hiện khác nhau giữa Long Header và Short Header
    - **Version:** Phiên bản QUIC, đối với Long Header
## Cách đưa QUIC Packer vào UDP Datagram
```text
+-------------------------------------------------------+
|                      IP PACKET                        |
|                                                       |
|  +-------------------------------------------------+  |
|  |                  IP Header                      |  |
|  +-------------------------------------------------+  |
|  |                 UDP Datagram                    |  |
|  |                                                 |  |
|  |  +-------------------------------------------+  |  |
|  |  | UDP Header                                |  |  |
|  |  +-------------------------------------------+  |  |
|  |  | QUIC Packet                               |  |  |
|  |  |                                           |  |  |
|  |  | +---------------------------------------+ |  |  |
|  |  | | QUIC Header                           | |  |  |
|  |  | +---------------------------------------+ |  |  |
|  |  | | Protected QUIC Payload                | |  |  |
|  |  | |                                       | |  |  |
|  |  | |   STREAM Frame                        | |  |  |
|  |  | |   ACK Frame                           | |  |  |
|  |  | |                                       | |  |  |
|  |  | +---------------------------------------+ |  |  |
|  |  +-------------------------------------------+  |  |
|  +-------------------------------------------------+  |
+-------------------------------------------------------+
```
## Thiết lập kết nối
Xét trường hợp thông thường: không sử dụng 0-RTT và không co Retry
### Các loại Packet của QUIC trong thiết lập kết nối
- Hai nhóm Header quan trọng: 
    - Long Header: Được sử dụng cho các loại Packet phục vụ thiết lập kết nối như Initial, Handshake, 0-RTT và Retry.
    - Short Header: Chủ yếu được sử dụng để truyền dữ liệu bằng 1-RTT khi đã có khóa thích hợp
- Các loại Packet cần biết:
    - Initial: Khởi đầu kết nối, mang dữ liệu TLS Handshake ban đầu.
    - Handshake: Tiếp tục trao đổi dữ liệu TLS Handshake
    - 0-RTT: Cho phép gửi dữ liệu ứng dụng sớm trong một số trường hợp kết nối lại
    - 1-RTT: Truyền dữ liệu sau khi các khóa 1-RTT đã sẵn sàng
### Luồng QUIC Handshake
```text
Sơ đồ cơ bản
client.c                                  server.c

   |                                          |
   | ---------- Initial Packet ------------>  |
   |             ClientHello                  |
   |                                          |
   | <--------- Initial Packet -------------- |
   | <-------- Handshake Packet ------------- |
   |             ServerHello                  |
   |             TLS Handshake data           |
   |                                          |
   | ---------- Handshake Packet ---------->  |
   |             Client Finished              |
   |                                          |
   | <---------- 1-RTT Packet --------------- |
   |             HANDSHAKE_DONE               |
   |                                          |
   |           QUIC CONNECTION                |
   |                                          |
   | ---------- STREAM Frame -------------->  |
   |             Hello Server!                |
   |                                          |
   | <--------- STREAM Frame ---------------- |
   |             Hello Client!                |
   |                                          |
```
**Giai đoạn 1: Client gửi Initial Packet**
- CLient gửi một QUIC Initial Packet, mang TLS ClientHello trong CRYPTO Frame.
**Giai đoạn 2: Server xử lý Initial Packet**
- Ở đây, Server nhận UDP Datagram bằng recvfrom(), sau đó ngtcp2 sẽ xử lý Packet. Thư viện ngtcp2 sẽ phân tích:
    - Phần tích QUIC Header
    - Xác định kết nối tương ứng
    - Xử lý bảo vệ và giải mã gói
    - Phân tích các QUIC Frame
    - Chuyển dữ liệu CRYPTO sang phần xử lý TLS
    - Phối hợp với GnuTLS để tiếp tục Handshake
**Giai đoạn 3: Hai phía thiết lập khóa**
Client và Server trao đổi những thông tin cần thiết để: 
- Thỏa thuận thuật toán bảo mật
- Xác thực Server thông qua chứng chỉ
- Tạo các khóa dùng để bảo vệ dữ liệu
QUIC sử dụng các khóa khác nhau cho từng cấp độ bảo vệ gói, bao gồm Initial, Handshake và 1-RTT
**Giai đóạn 4: Truyền dữ liệu ứng dụng**
Khi có khóa 1-RTT và trạng thái kết nối cho phép, ứng dụng có thể bắt đầu trao đổi dữ liệu qua QUIC Stream.
## Cách QUIC nhận dữ liệu ở Server
```text
                       SERVER

                       Network
                          |
                          v
                     UDP Datagram
                          |
                          v
                     recvfrom()
                          |
                          v
                     UDP Buffer
                          |
                          v
                ngtcp2_conn_read_pkt()
                          |
             +------------+------------+
             |            |            |
             v            v            v
          Header        Decrypt       Frames
          Parsing       Packet        Parsing
                                       |
                              +--------+--------+
                              |                 |
                              v                 v
                         CRYPTO Frame      STREAM Frame
                              |                 |
                              v                 v
                            TLS            Stream Data
                              |                 |
                              v                 v
                          Handshake          Callback
                                                |
                                                v
                                          Application
```
## Các hàm triển khai QUIC với thư viện ngtcp2
### 1. Phân biệt ngtcp2_conn và ngtcp2_conn_server_new()
    **ngtcp2_conn *conn = NULL;**
Ở đây:
- ngtcp2_conn là một kiểu dữ liệu.
- conn là con trỏ đến đối tượng lưu trữ trạng thái của một kết nối QUIC.
- ngtcp2_conn_server_new() là hàm và khởi tạo đối tượng đó.
Tóm lại:
- Khi server nhận được QUIC Packet từ Client mới và xác định cần tạo kết nối, lúc này nó sẽ gọi hàm ngtcp2_conn_server_new().
- Khi đó, hàm sẽ thực hiện một số công việc như
    - Cấp phát bộ nhớ cho ngtcp2_conn
    - Khởi tạo các trạng thái ban đầu của kết nối
    - Thiết lập ConnectionID, đường truyền và các cấu hình cần thiết.
    - Lưu những thông tin cần thiết để xử lý các Packet tiếp theo.
- Sau khi hàm thực hiện thành công, chúng ta sẽ có:
```text
           conn
             |
             v
    +----------------------+
    | QUIC Connection      |
    |----------------------|
    | Connection IDs       |
    | Handshake state      |
    | Packet state         |
    | Stream state         |
    | Flow control         |
    | Congestion control   |
    | ...                  |
    +----------------------+
```
- Chữ ký hàm: 
ngtcp2_conn_server_new(
    &conn,
    &dcid,
    &scid,
    &path,
    version,
    &callbacks,
    &settings,
    &params,
    NULL,
    NULL
);
- Nhóm định danh đường truyền: Bao gồm dcid, scid, path,version nhằm xác định kết nối khởi tạo
- Nhóm cấu hình giao thức: callbacks, settings, params nhằm quy định cách kết nối hoạt động
- Nhóm quản lý đối tượng: conn, mem, user_data nhằm tạo đối tượng và gắn dữ liệu ứng dụng.
- Ở đây, hàm ngtcp2_conn_read_pkt(conn, ...); sẽ sử dụng thông tin trong Packet để xử lý và cập nhật trạng thái kết nối.
### 2. Hàm ngtcp2_accept()
- Hàm được triển khai như sau:
    ngtcp2_accept(
        &hd,
        (const uint8_t *) buffer,
        (size_t)byte_received
    );
- Ở đây, hàm sẽ lưu dữ liệu các dcid, scid,... sau khi đọc và phần tích từ buffer với số byte sẽ đọc từ byte_received.
- Bên trong ngtcp2_pkt_hd có cấu trúc như sau:
    typedef struct ngtcp2_pkt_hd {
        ngtcp2_cid dcid;
        ngtcp2_cid scid;

        int64_t pkt_num;

        const uint8_t *token;
        size_t tokenlen;

        size_t pkt_numlen;
        size_t len;

        uint32_t version;
        uint8_t type;
        uint8_t flags;
    } ngtcp2_pkt_hd;
- Tức là nó sẽ bao gồm các thông tin về header bao gồm: Destination CID, Source CID, Version QUIC, Type QUIC Packet, Packet Number, Packet Number len (số byte dùng để mã hóa Packet Number), len (độ dài phần Packet Number cộng với payload)
- Kiểu ngtcp2_cid thực chất có dạng:
typedef struct {
    size_t datalen;
    uint8_t data[NGTCP2_MAX_CIDLEN];
} ngtcp2_cid;
### 3. ngtcp2_transport_params
- Nó thể hiện các cấu trúc chứa những giới hạn và khả năng của QUIC mà 1 đầu kết nối công bố cho 1 đầu còn lại trong quá trình Hanshake. Gồm: giới hạn dữ liệu nhận, giới hạn số lượng stream, thời gian hoạt động, thông tin định danh trong quá trình thiết lập kết nối.
```text
ngtcp2_transport_params params;

ngtcp2_transport_params_default(&params);

// Flow Control ở cấp Connection
params.initial_max_data = 1024 * 1024;

// Flow Control ở cấp Stream
params.initial_max_stream_data_bidi_local = 64 * 1024;
params.initial_max_stream_data_bidi_remote = 64 * 1024;
params.initial_max_stream_data_uni = 64 * 1024;

// Giới hạn số Stream
params.initial_max_streams_bidi = 10;
params.initial_max_streams_uni = 10;

// Thời gian không hoạt động
params.max_idle_timeout = 30 * NGTCP2_SECONDS;

// Original DCID của kết nối mới, không qua Retry
params.original_dcid = hd.dcid;
params.original_dcid_present = 1;
```
- Hàm ngtcp2_transport_params_default(&params); có 2 nhiệm vụ:
    - Đặt toàn bộ cấu trúc về giá trị 0
    - Gán các giá trị mặc định cho 4 trường, bao gồm: kích thước của UDP tối đa được công bố, hệ số dùng để giải mã ACK delay, độ trễ tối đa ACK được công bố, giới hạn connection ID đang hoạt động được cung cấp cho phía này.

### 4. ngtcp2_path và ngtcp2_addr
typedef struct {
    ngtcp2_addr local;
    ngtcp2_addr remote;
    void *user_data;
} ngtcp2_path;
typedef struct {
    ngtcp2_sockaddr *addr;
    ngtcp2_socklen addrlen;
} ngtcp2_addr;

### 5. ngtcp2_settings
- Là cấu trúc chứa các cấu trúc để điều khiển hoạt động của QUIC CONNECTION đang chạy ở phía chương trình đang chạy.
- Chúng ta có thể hiểu rằng:
    - ngtcp2_transport_params sẽ giới hạn và khả năng công bố cho client
    - ngtcp2_settings sẽ chứa cách ngtcp2 vận hành
- Trong ngtcp2_settings có các trường như sau:
    - cc_algo (ngtcp2_cc_algo): Thuật toán điều khiển tắc nghẽn
    - initial_ts (ngtcp2_tstamp): Thời điểm bắt đầu Connection
    - initial-rtt (ngtcp2_duration): Giá trị RTT ước lượng ban đầu
    - max_tx_udp_payload_size (size_t): Kích thước Payload tối đa mà phía Client có thể tạo và gửi
    - handshake_timeout (ngtcp2_duration): Thời gian tối đa cho phép hoàn thành handshake
    - ack_thresh (số nguyên): Ngưỡng ACK mà ngtcp2 sử dụng trong cơ chế xác nhận
    - qlog_write (con trỏ hàm): callback ghi lại dữ liệu qlog phục vụ phân tích
    - max_window (số nguyên): Giới hạn cửa sổ nhận tự động ở cấp Connection
    - max_stream_window (số nguyên): Giới hạn cửa sổ nhận tự động ở cấp Stream

ngtcp2_settings settings
│
├── initial_ts
├── initial_rtt
├── handshake_timeout
├── cc_algo
├── max_tx_udp_payload_size
├── ack_thresh
├── qlog_write
└── ...
hàm trước tiên đặt cấu trúc về giá trị 0, sau đó thiết lập một số giá trị mặc định, chẳng hạn:
| Trường | Giá trị mặc định |
|---|---|
| `cc_algo` | CUBIC |
| `initial_rtt` | 333 ms |
| `ack_thresh` | 2 |
| `max_tx_udp_payload_size` | 1452 byte |
| `handshake_timeout` | `UINT64_MAX` |
