# Học code MQTT over QUIC từ luồng chạy thực tế

Tài liệu dựa trên source đã tách logic publisher/subscriber, ngày 09/11/2026.
Hai executable đã build và chạy lại thành công trên host và Mininet h1/h2.

Thư mục project:

```text
/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic
```

## 1. Bốn câu hỏi cần trả lời trước khi đọc hàm

1. **Ai tạo dữ liệu?** Publisher C trên h1.
2. **Ai chọn nơi nhận?** EMQX dựa vào MQTT topic và subscription.
3. **Ai nhận dữ liệu?** Subscriber C trên h2.
4. **Ai vận chuyển byte?** ngtcp2 thực hiện QUIC trong mỗi client; GnuTLS
   tham gia TLS 1.3. EMQX có triển khai QUIC phía server của broker.

```text
Publisher C                 EMQX                  Subscriber C
    ngtcp2                  broker                    ngtcp2
      │                       │                         │
      └── QUIC connection A ──┤── QUIC connection B ────┘
```

Cả publisher và subscriber đều chủ động kết nối EMQX. Subscriber là client
về vai trò kết nối mạng, dù nhiệm vụ nghiệp vụ của nó là nhận message.

Mỗi connection dùng một bidirectional stream, thường là stream ID 0. Stream
0 của connection A và stream 0 của connection B là hai stream khác nhau.
EMQX kết thúc QUIC ở phía broker, đọc MQTT và gửi một MQTT PUBLISH tới người
đăng ký. Không có QUIC connection trực tiếp giữa h1 và h2.

So với demo echo cũ: client cũ gửi byte và server C trả lại đúng byte đó.
Phiên bản này thêm MQTT topic, subscription và broker; server echo cũ không
chạy trong bài MQTT. Hai client mới vẫn dùng ngtcp2 1.25.0 từ bản build cũ.

## 2. Bản đồ source và thứ tự đọc

| Đọc | File | Câu hỏi cần giải thích được |
|---|---|---|
| 1 | `clients/subscriber.c` | Khi nào gửi SUBSCRIBE? Khi nào được nhận PUBLISH? |
| 2 | `clients/publisher.c` | Khi nào gửi message? Khi nào được đóng connection? |
| 3 | `clients/app.h`, `clients/app.c` | Hai vai trò dùng chung session và callbacks ra sao? |
| 4 | `clients/mqtt_codec.h`, `clients/mqtt_codec.c` | Struct trong C trở thành byte trên stream như thế nào? |
| 5 | `clients/quic_client.h`, `clients/quic_client.c` | Byte đi vào QUIC packets, socket và ngược lại ra sao? |
| 6 | `mininet/topology.py` | h1/h2 tới broker qua mạng nào? |
| 7 | `docker/compose.yaml`, `docker/emqx.conf` | Broker lắng nghe ở đâu, dùng chứng chỉ nào? |

Mở source cùng tài liệu. Đọc một hàm, viết lại bằng một câu tiếng Việt, rồi
theo hàm nó gọi. Khi quay lại phần phức tạp, bạn đã biết nó phục vụ việc gì.

## 3. Chạy một lần để có log làm mốc

Chạy từ thư mục project:

```bash
make
./scripts/broker.sh up
```

Terminal 1:

```bash
./clients/subscriber --host 127.0.0.1
```

Đợi `MQTT SUBACK accepted`, rồi ở terminal 2:

```bash
./clients/publisher --host 127.0.0.1 --message 'Temperature = 28.5 C'
```

Quan sát subscriber nhận topic/payload, publisher thoát, subscriber tiếp tục
chạy. Sau khoảng 10 giây subscriber không gửi MQTT packet, bạn sẽ thấy
PINGREQ/PINGRESP. Nhấn Ctrl+C để kết thúc subscriber có DISCONNECT.

Địa chỉ mặc định của executable là `10.77.0.254`, vì đó là yêu cầu thiết kế
gốc. Khi chạy trên host phải ghi rõ `--host 127.0.0.1`. Khi test Mininet trên
máy này, topology dùng subnet thay thế và tự truyền `10.78.0.254` cho client.

## 4. Bài đầu: đọc subscriber từ main()

Trong `clients/subscriber.c`, bắt đầu ở `main()`, khoảng dòng 113:

```c
subscriber_context subscriber = {.state = WAIT_SUBACK};
const mqtt_role role = {
    .name = "subscriber",
    .subscriber = 1,
    .default_timeout = 0,
    .connected = subscriber_connected,
    .packet = subscriber_packet,
    .tick = subscriber_tick
};
return mqtt_app_main(argc, argv, &role, &subscriber);
```

`subscriber` chứa dữ liệu riêng của process nhận: trạng thái đăng ký, số
message đã nhận, thời điểm ping và thời hạn SUBACK. Nó không chứa socket của
publisher, cũng không truy cập vùng nhớ của process publisher.

`role` là bảng các hàm cần gọi khi có sự kiện:

| Trường | Khi được gọi | Subscriber làm gì |
|---|---|---|
| `connected` | Sau MQTT CONNACK hợp lệ | Gửi SUBSCRIBE |
| `packet` | Khi parser có MQTT packet hoàn chỉnh sau CONNACK | Xử lý SUBACK, PUBLISH hoặc PINGRESP |
| `tick` | Event loop kiểm tra định kỳ | Kiểm tra timeout, gửi PINGREQ |

Chú ý: tên `connected` ở bảng này nghĩa là **MQTT CONNECT đã được broker
chấp nhận**, không chỉ có UDP socket hay QUIC handshake.

`default_timeout = 0` nghĩa là không giới hạn tổng thời gian chạy subscriber.
Handshake, chờ CONNACK/SUBACK và chờ PINGRESP vẫn có timeout riêng.

Trường `.subscriber = 1` chỉ giúp phần CLI chọn `--count` và hiển thị help;
logic đăng ký/nhận message nằm trong ba hàm ở source subscriber.

### 4.1. subscriber_connected(): đăng ký topic

Xem khoảng dòng 20:

```c
int result = mqtt_encode_subscribe(&buffer, 1, app->topic);
if (mqtt_app_send(app, &buffer, result)) return -1;
subscriber->state = WAIT_SUBACK;
subscriber->subscribe_deadline = quic_client_now_ms() + 15000;
```

Diễn giải:

1. Tạo MQTT SUBSCRIBE cho một topic, yêu cầu QoS 0, packet identifier là 1.
2. Đưa các byte vào hàng đợi QUIC.
3. Chuyển trạng thái sang chờ SUBACK.
4. Chỉ chờ tối đa 15 giây ở bước này.

`mqtt_app_send()` trả 0 nghĩa là packet được mã hóa và đưa vào hàng đợi.
Nó chưa có nghĩa là broker đã nhận hoặc chấp nhận subscription. Dòng log
`SUBSCRIBE sent` đánh dấu việc queue packet trong ứng dụng.

### 4.2. subscriber_packet(): nhận SUBACK

Nhánh `packet->type == 9` kiểm tra:

```c
mqtt_decode_suback(packet, &packet_id, &granted_qos)
packet_id == 1
granted_qos == 0
```

Type 9 là MQTT SUBACK. Packet identifier phải khớp SUBSCRIBE đang chờ, và
broker phải chấp nhận QoS 0. Sau đó mới đổi sang `SUBSCRIBED` và in
`MQTT SUBACK accepted`. Chờ dòng này trước khi publish giúp tránh gửi message
trước khi subscription tồn tại.

Các giá trị `WAIT_SUBACK`/`SUBSCRIBED` là trạng thái nội bộ C. Chúng không
phải QUIC stream ID hay MQTT packet type.

### 4.3. subscriber_packet(): nhận PUBLISH

Nhánh `packet->type == 3 && state == SUBSCRIBED`:

1. Gọi `mqtt_decode_publish()` lấy topic và payload.
2. Kiểm tra topic nhận được khớp topic đã đăng ký và packet thuộc QoS 0.
3. In topic/payload theo độ dài.
4. Tăng `received`.
5. Nếu đã đủ `--count`, gửi DISCONNECT và kết thúc có kiểm soát.

```c
fwrite(message.payload, 1, message.payload_len, stdout);
```

Không dùng `printf("%s", message.payload)` vì payload MQTT không bắt buộc
kết thúc bằng byte NUL và có thể chứa byte NUL ở giữa. Topic trong packet
cũng được biểu diễn bằng con trỏ + độ dài, không phải C string đã kết thúc.

`mqtt_publish` chỉ giữ các con trỏ nhìn vào packet parser đang xử lý. Chúng
chỉ hợp lệ trong callback này. Nếu sau này muốn lưu message để xử lý muộn,
bạn phải cấp vùng nhớ riêng và copy đúng số byte trước khi callback trả về.

### 4.4. subscriber_tick(): duy trì kết nối

Hàm tick không tự có thread riêng. Event loop của QUIC gọi nó định kỳ.

```text
WAIT_SUBACK quá 15 giây          → lỗi
SUBSCRIBED, im lặng gửi 10 giây  → PINGREQ, đặt ping_pending
Nhận PINGRESP                   → xóa ping_pending
Chờ PINGRESP quá 10 giây         → lỗi
```

CONNECT công bố keepalive 20 giây. PINGREQ sau 10 giây giúp tạo lưu lượng MQTT
trước thời hạn đó. QUIC ACK và MQTT PINGRESP là hai loại thông tin khác nhau:
ACK thuộc vận chuyển QUIC; PINGRESP thuộc giao thức ứng dụng MQTT.

## 5. Đọc publisher để so sánh

Publisher có source riêng trong `clients/publisher.c`:

```text
main()
  → mqtt_app_main()
  → QUIC/TLS + MQTT CONNECT/CONNACK
  → publisher_connected()
  → publisher_tick()
  → mqtt_app_disconnect()
```

`publisher_connected()` mã hóa MQTT PUBLISH QoS 0 từ `app->topic` và
`app->message`, đưa vào hàng đợi rồi đặt deadline 15 giây.

`publisher_tick()` đợi:

```c
quic_client_pending(app->quic) == 0
```

Điều này nghĩa là toàn bộ byte đã queue được ngtcp2 báo ACK. Publisher sau đó
queue MQTT DISCONNECT và yêu cầu QUIC đóng có kiểm soát.

QoS 0 không có MQTT PUBACK. QUIC ACK xác nhận phía QUIC peer đã nhận stream
bytes, chưa chứng minh EMQX đã chuyển message tới subscriber. Vì vậy bài test
phải kiểm tra cả log nhận đúng topic/payload trên process subscriber.

Publisher CLI lấy payload bằng `strlen()`. Codec hỗ trợ payload nhị phân,
nhưng giao diện `--message` hiện chỉ nhập C string, không nhập byte NUL giữa
chuỗi. Đừng nhầm khả năng của codec với khả năng của CLI.

## 6. Con trỏ hàm và void*: phần C cần hiểu rõ

Trong `app.h`:

```c
int (*packet)(mqtt_app *, const mqtt_packet *);
```

Đây là biến giữ **địa chỉ một hàm**. Hàm đó nhận hai con trỏ và trả `int`.

Khi subscriber gán:

```c
.packet = subscriber_packet
```

và session chung gọi:

```c
app->role->packet(app, packet);
```

thì chương trình đang gọi `subscriber_packet(app, packet)`. Publisher gán
hàm riêng nên cùng lời gọi dispatch sẽ đi tới `publisher_packet()`.

`void *role_data` là địa chỉ dữ liệu riêng của vai trò. Subscriber khôi phục
kiểu thực tế bằng:

```c
subscriber_context *subscriber = app->role_data;
```

Trong C, phép chuyển từ `void *` về con trỏ object được thực hiện trực tiếp.
Compiler không kiểm chứng role_data có đúng kiểu subscriber_context; phần
khởi tạo phải bảo đảm đúng. Publisher truyền địa chỉ struct publisher riêng.

Ba tầng dữ liệu callback cần phân biệt:

```text
ngtcp2 user_data       → struct quic_client
quic_client c->user    → struct mqtt_app
mqtt_app role_data     → subscriber_context hoặc publisher
```

Các struct `subscriber`, `role` ở main() tồn tại trên stack của process.
Chúng vẫn còn hợp lệ vì `mqtt_app_main()` và event loop chạy đồng bộ, chỉ
trả về khi tất cả callbacks đã kết thúc. API này không tạo background thread
tiếp tục dùng các địa chỉ đó sau khi main() thoát.

## 7. Session chung app.c làm gì?

### 7.1. mqtt_app_main()

Hàm tạo cấu hình QUIC, đọc CLI, sinh client ID có PID để tránh trùng mặc
định, tạo `mqtt_app`, đăng ký `quic_callbacks`, rồi gọi `quic_client_run()`.
Hai chương trình link cùng object session/codec/transport nhưng có main và
callbacks nghiệp vụ khác nhau. Chúng là hai process độc lập khi chạy.

### 7.2. on_ready()

QUIC chỉ gọi callback ready sau handshake TLS được xác minh và mở được
bidirectional stream. Lúc đó app mới queue MQTT CONNECT với clean session,
MQTT version 3.1.1 và keepalive 20 giây.

### 7.3. on_packet()

Khi chưa connected, packet phải là CONNACK hợp lệ. App kiểm tra return code
0, session present phù hợp clean session rồi gọi `role->connected()`.
Các packet tiếp theo được chuyển tới `role->packet()`.

### 7.4. on_data()

Transport đưa **byte stream** vào đây, chưa chắc là MQTT packet hoàn chỉnh.
Hàm gọi `mqtt_parser_feed()`; parser gọi `on_packet()` khi đã có đủ packet.

### 7.5. mqtt_app_send()

Hàm kiểm tra lỗi encoder, gọi `quic_client_send()` để copy dữ liệu sang queue
của transport, giải phóng buffer encoder rồi cập nhật thời điểm gửi MQTT.
Việc free ngay ở đây an toàn vì transport đã copy; không giao con trỏ heap
sắp free trực tiếp cho ngtcp2 giữ để truyền lại.

## 8. Đi theo một message ở chiều gửi

```text
publisher_connected()
  → mqtt_encode_publish()             tạo các byte MQTT
  → mqtt_app_send()
  → quic_client_send()                copy vào ring buffer

event_loop()
  → flush_packets()
  → ngtcp2_conn_writev_stream()       ngtcp2 tạo QUIC packets
  → send_datagram()
  → send()                           UDP socket gửi QUIC packets
```

Hai đoạn này xảy ra ở những lượt event loop phù hợp, không phải encoder
gọi thẳng socket. Khi bị congestion/flow control chặn, ngtcp2 có thể chưa
nhận hết byte để gửi ngay. Code giữ offset và tiếp tục ở lượt sau.

EMQX nhận QUIC packets, khôi phục stream bytes, đọc MQTT PUBLISH, tra topic
và đưa một PUBLISH vào connection của subscriber.

## 9. Chiều nhận: từ UDP đến subscriber_packet()

```text
read_packets()
  → recv()                           đọc UDP datagram
  → ngtcp2_conn_read_pkt()            xác thực, giải mã, xử lý QUIC
  → receive_stream()                 ngtcp2 cung cấp stream bytes đúng thứ tự
  → callbacks.data = on_data()
  → mqtt_parser_feed()               ghép đủ MQTT packet
  → on_packet()
  → role.packet = subscriber_packet()
  → mqtt_decode_publish()
  → fwrite(topic), fwrite(payload)
```

`receive_stream()` kiểm tra stream ID, offset, trạng thái FIN và từ chối
early data. Sau khi ứng dụng tiêu thụ byte, nó mở rộng giới hạn nhận stream
và connection để tiếp tục flow control. Retransmission/reordering do ngtcp2
giải quyết; MQTT parser chỉ nhìn chuỗi byte theo thứ tự.

## 10. MQTT PUBLISH ở mức từng byte

Với topic `lab/temperature` và payload `Temperature = 28.5 C`:

```text
topic length   = 15 byte
payload length = 20 byte
Remaining Length = 2 + 15 + 20 = 37 = 0x25
packet length = 1 + 1 + 37 = 39 byte
```

```text
30 25 00 0f
6c 61 62 2f 74 65 6d 70 65 72 61 74 75 72 65
54 65 6d 70 65 72 61 74 75 72 65 20 3d 20 32 38 2e 35 20 43
```

| Đoạn | Nghĩa |
|---|---|
| `30` | MQTT PUBLISH type 3, QoS 0, DUP=0, RETAIN=0 |
| `25` | 37 byte còn lại sau trường Remaining Length |
| `00 0f` | Topic dài 15 byte, số nguyên hai byte big-endian |
| 15 byte tiếp | UTF-8 topic |
| 20 byte cuối | Payload |

PUBLISH QoS 0 không có packet identifier và không có trường payload length
riêng. Decoder suy ra payload length từ Remaining Length trừ phần variable
header. Vì topic/payload có độ dài xác định, không thêm byte NUL kết thúc.

Một số packet khác trong bài:

```text
CONNACK success : 20 02 00 00
SUBSCRIBE      : 82 14 00 01 00 0f [15 byte topic] 00
SUBACK QoS 0   : 90 03 00 01 00
PINGREQ        : c0 00
PINGRESP       : d0 00
DISCONNECT     : e0 00
```

Trong SUBSCRIBE, `00 01` là packet identifier 1, `00` cuối là QoS yêu cầu.
SUBACK trả lại đúng identifier đó.

Flags `0x2` trong header SUBSCRIBE là flags bắt buộc của loại packet này;
requested QoS nằm ở byte cuối body. Client lab chỉ đăng ký một topic chính
xác và cấm `+`/`#` trong codec. MQTT nói chung có hỗ trợ wildcard topic filter;
đây là giới hạn phạm vi của chương trình hiện tại.

Phần đầu variable header của CONNECT:

```text
00 04 4d 51 54 54 04 02 00 14
```

Đó là độ dài tên giao thức 4, chữ MQTT, protocol level 4 (MQTT 3.1.1), flags
clean session và keepalive 20 giây. Sau đó là hai byte độ dài client ID và
client ID; giá trị cụ thể phụ thuộc PID hoặc `--client-id`.

### Remaining Length nhiều byte

Remaining Length dùng base 128; bit cao nhất cho biết còn byte tiếp theo.
Với giá trị 128:

```text
128 % 128 = 0, còn thương 1 → 80
  1 % 128 = 1, hết          → 01
```

Nên 128 được mã hóa `80 01`. Nó không phải số nguyên 16 bit big-endian như
topic length. Codec cho phép tối đa bốn byte của trường này, đồng thời giới
hạn kích thước packet trong lab là 1 MiB để giới hạn bộ nhớ.

## 11. Vì sao phải ghép packet từ stream?

Một MQTT PUBLISH dài 39 byte có thể được callback nhận dưới dạng:

```text
lần 1: 30
lần 2: 25 00 0f [một phần topic]
lần 3: [topic còn lại + payload]
```

Hoặc một callback chứa nhiều packet MQTT liên tiếp. Không được giải mã toàn
bộ một lần recv/receive_stream như thể nó là đúng một message.

`mqtt_parser_feed()` giữ `data`, `len`, `cap`:

1. Thu thập từng byte header và đọc Remaining Length.
2. Kiểm tra giới hạn trước khi cấp bộ nhớ cho body.
3. Chờ đủ `header_length + body_length`.
4. Tạo `mqtt_packet` trỏ vào buffer, gọi callback.
5. Giữ phần thừa và tiếp tục nếu còn packet khác.

Nếu đã có 10/39 byte, nó giữ 10 byte và chờ tiếp. Nếu có hai packet gộp,
nó gọi handler hai lần. Test codec đã kiểm tra các trường hợp chia/gộp này.

## 12. Khởi tạo QUIC/TLS: đọc quic_client_run()

Đi từ cuối `clients/quic_client.c`, khoảng dòng 542:

```text
quic_client_run()
  → cấp struct quic_client, khởi tạo GnuTLS, đăng ký signal handlers
  → socket_open()
  → connection_init()
  → tls_init()
  → event_loop()
  → giải phóng QUIC, TLS credentials và UDP socket
```

### socket_open()

Tạo socket `AF_INET, SOCK_DGRAM`, đặt nonblocking rồi gọi UDP `connect()` để
chọn peer và xác định IP/port nguồn. UDP connect không thực hiện handshake
QUIC và không có TCP handshake. Handshake QUIC bắt đầu từ packets do ngtcp2
tạo trong event loop.

### connection_init()

Chuẩn bị callbacks, settings, transport parameters và connection IDs ngẫu
nhiên rồi gọi `ngtcp2_conn_client_new()`. Chỉ mở application stream do client
khởi tạo; code không nhận stream ứng dụng mới từ broker.

Connection ID giúp QUIC nhận diện connection; stream ID nhận diện luồng byte
trong connection. Chúng không phải MQTT client ID hay topic.

### tls_init()

Nạp CA, tạo GnuTLS client session, cấu hình backend QUIC của ngtcp2, chỉ cho
TLS 1.3, đặt SNI `broker.test`, bật kiểm tra certificate và ALPN `mqtt`.

Client kết nối IP có route được, nhưng xác minh danh tính DNS của broker.
CA kiểm tra nguồn tin cậy, tên server kiểm tra đúng danh tính, và validity
kiểm tra thời gian certificate. ALPN giúp hai phía thống nhất giao thức ứng
dụng sẽ chạy trên QUIC connection này.

GnuTLS cung cấp TLS handshake/khóa; ngtcp2 phối hợp xử lý QUIC CRYPTO frames
và bảo vệ packets. MQTT không được bọc trong TLS records kiểu TLS-over-TCP;
application bytes nằm trong QUIC STREAM frames được bảo vệ bằng khóa QUIC.

### handshake_completed() và open_stream()

Callback kiểm tra lại TLS 1.3, ALPN và certificate. Sau khi có handshake hợp
lệ, event loop thử `ngtcp2_conn_open_bidi_stream()`. Nếu broker chưa cấp
stream credit, nó chờ tiếp. Khi mở được, mới gọi `on_ready()` của app.

## 13. Event loop, timers và ACK

Mỗi vòng `event_loop()` xử lý signal, timeout, ngtcp2 expiry, mở stream nếu
đủ điều kiện, gọi app tick, flush packets, rồi poll socket. Khi socket có dữ
liệu nó gọi read_packets. `poll()` chờ tối đa khoảng 20 ms trong nhánh chờ
thông thường, hoặc ngắn hơn nếu QUIC timer sắp đến hạn; đây không phải bảo
đảm thời gian thực cứng vì còn thời gian xử lý callbacks/socket.

Khi đến expiry, code gọi `ngtcp2_conn_handle_expiry()`. ngtcp2 xử lý loss
detection/PTO và chuẩn bị dữ liệu truyền lại; `flush_packets()` tiếp tục tạo
packets. Ứng dụng không tự gửi lại MQTT PUBLISH mỗi lần mất UDP packet.

Ba counter gửi cần nhớ:

| Biến | Ý nghĩa |
|---|---|
| `tx_queued` | Tổng số byte app đã copy vào queue |
| `tx_sent` | Tổng số byte ngtcp2 đã consume từ app để truyền |
| `tx_acked` | Tổng số byte được ngtcp2 báo ACK theo stream offset |

```text
tx_acked ≤ tx_sent ≤ tx_queued
pending = tx_queued - tx_acked
```

`tx_sent` tăng theo `consumed` trả về từ writev_stream. Nó chưa chứng minh
datagram đã được broker nhận. `tx_acked` chỉ tăng trong `acked_stream()`.

Ví dụ CONNECT dài 35 byte và PUBLISH dài 39 byte:

```text
CONNECT đã ACK: queued=35 sent=35 acked=35
Queue PUBLISH:  queued=74 sent=35 acked=35
ngtcp2 consume: queued=74 sent=74 acked=35
Nhận ACK:      queued=74 sent=74 acked=74, pending=0
```

CONNECT length phụ thuộc client ID, nên 35 là ví dụ tương ứng một lần chạy,
không phải hằng số giao thức.

## 14. Bộ nhớ và shutdown

Ring buffer truyền có kích thước 1 MiB, giữ địa chỉ ổn định. Byte chưa ACK
phải được giữ vì ngtcp2 có thể cần đọc lại khi truyền lại. Chỉ vùng đã được
ACK mới có thể tái sử dụng khi queue quay vòng. Đây là lý do không free hoặc
di chuyển buffer chỉ vì lần send() đầu đã thành công.

Shutdown bình thường:

```text
application queue MQTT DISCONNECT
  → mqtt_app_disconnect() gọi quic_client_finish()
  → finish chỉ đặt cờ finishing và deadline
  → event loop drain tất cả application bytes tới ACK
  → gửi FIN rỗng ở cuối stream
  → chờ FIN ACK
  → QUIC_DRAIN_OK
  → gửi CONNECTION_CLOSE và xử lý closing trong thời gian giới hạn
  → free QUIC, TLS, socket
```

DISCONNECT là packet MQTT. FIN kết thúc một chiều QUIC stream. CLOSE kết
thúc QUIC connection. Chúng có tác dụng ở ba mức khác nhau.

Code cũng có nhánh broker chủ động CLOSE: chỉ chấp nhận đóng bình thường
khi đang finishing, đã gửi FIN, hết application bytes chờ ACK và peer báo
error code 0. Nhánh này không đợi callback FIN ACK riêng sau khi peer đã
đóng connection. Các lỗi transport/TLS, stream kết thúc bất ngờ hoặc drain
timeout được trả về thất bại.

## 15. Mininet và Docker trong luồng thực tế

Subnet chạy thử ở máy này là `10.78.0.0/24`:

```text
h1 10.78.0.11 ─┐
                s1 standalone ─ mn-gw 10.78.0.254 ─ Ubuntu ─ Docker ─ EMQX
h2 10.78.0.12 ─┘
```

`mn-gw` là OVS internal port trên host, không phải process MQTT gateway.
Docker publish UDP 14567 chuyển gói tới broker. Không có veth tự nối trực
tiếp vào container, không có MQTT proxy, không có NAT riêng của Mininet.

Đọc `main()` của topology theo thứ tự: preflight → root/runtime → khóa tên
tài nguyên → tạo h1/h2/s1 → start → thêm mn-gw → IP/up → ping → test/CLI →
finally cleanup có kiểm tra ownership. Preflight giữ an toàn cho bridge VM
`virbr-qdata` đang dùng subnet gốc `10.77.0.0/24`.

```bash
python3 scripts/preflight.py --subnet 10.78.0.0/24 --json
sudo /usr/bin/python3 mininet/topology.py --subnet 10.78.0.0/24 --test
```

Ping chứng minh IP. Log handshake chứng minh QUIC/TLS. CONNACK chứng minh
MQTT session. SUBACK chứng minh subscription. Dòng topic/payload trên h2
chứng minh message được chuyển qua broker. Đọc từng bằng chứng theo đúng tầng.

## 16. Học bằng debugger

Máy đã có gdb. Nên debug trên host trước để không cần kết hợp sudo/namespace.
Nếu muốn biến local dễ quan sát, build bản `-O0`:

```bash
make -C clients clean
make -C clients CFLAGS='-O0 -g -std=c11 -Wall -Wextra -Wpedantic'
gdb --args ./clients/subscriber --host 127.0.0.1 --count 1
```

Trong gdb:

```text
break main
break on_ready
break subscriber_connected
break subscriber_packet
break subscriber_tick
run
```

Dùng `continue` đi tới callback tiếp theo; `next` chạy qua lời gọi hàm;
`step` đi vào hàm; `bt` xem call stack; `list` xem source quanh vị trí hiện
tại. Đừng dừng quá lâu ở breakpoint khi connection đã chạy vì broker/QUIC
có timeout theo thời gian thực.

Ở `subscriber_connected()`:

```text
print app->topic
print app->connected
print *(subscriber_context *)app->role_data
```

Ở `subscriber_packet()`:

```text
print packet->type
print packet->len
x/3bx packet->body
```

Nếu đó là SUBACK QoS 0 của packet ID 1, body là `00 01 00`. Khi đến PUBLISH,
body bắt đầu bằng topic length `00 0f`. Packet type và body là kết quả MQTT
parser đã tách header; body không còn byte đầu `30` và Remaining Length.

Muốn xem chiều gửi, đặt breakpoint `mqtt_encode_subscribe`,
`quic_client_send`, `flush_packets`. Xem `c->tx_queued`, `c->tx_sent`,
`c->tx_acked` tại `acked_stream`. Dùng `continue` nhanh để tránh timeout.

## 17. Bài tập theo thứ tự, kèm kết quả mong đợi

1. **Một message:** chạy hai terminal ở phần 3. Giải thích từng log từ
   QUIC_CONNECTING tới PUBLISH received. Phải chỉ ra thời điểm subscription
   được broker chấp nhận.
2. **Hai message:** subscriber dùng `--count 2`; chạy publisher hai lần với
   payload khác nhau. Subscriber nhận hai message rồi mới disconnect.
3. **Topic khác nhau:** subscriber topic `lab/temperature`, publisher topic
   `lab/other`. Subscriber không nhận publication đó; giải thích quyết định
   routing thuộc EMQX. Dùng `--timeout 15` để bài tập tự dừng có lỗi timeout.
4. **Keepalive:** chỉ chạy subscriber, chờ 12 giây. Tìm PINGREQ/PINGRESP và
   lần cập nhật `last_tx`/`ping_pending`.
5. **Packet lengths:** với topic mặc định, thử tính payload 110 byte làm
   Remaining Length=127 và payload 111 byte làm Remaining Length=128. Trường
   RL lần lượt là `7f` và `80 01`; kích thước header tăng một byte.
6. **Mất ranh giới packet:** trên giấy chia PUBLISH 39 byte thành 1+2+36
   byte. Theo dõi parser.len ở từng lần feed. Sau đó đọc test_codec.c xem
   bài test đang kiểm chứng điều tương tự như thế nào.
7. **Xác minh TLS:** chạy publisher với `--server-name wrong.test`. Phải
   thất bại trước MQTT CONNECT/CONNACK; tìm TLS_CERTIFICATE_REJECTED.
8. **ACK và nhận message:** giải thích vì sao pending=0 ở publisher chưa
   đủ để xác nhận subscriber nhận được. Đưa ra log cần bổ sung làm bằng chứng.
9. **Đóng kết nối:** nhấn Ctrl+C subscriber và theo dõi DISCONNECT → drain
   → FIN → CLOSE. Chỉ ra chỗ handler signal đặt cờ thay vì làm socket I/O.
10. **Mininet:** chạy bài --test, đối chiếu endpoint `.254`, h1 `.11`, h2
    `.12`, rồi kiểm tra interface của lab đã biến mất sau cleanup.

## 18. Các câu hỏi tự kiểm tra và đáp án ngắn

| Câu hỏi | Đáp án |
|---|---|
| Subscriber có cần bind UDP 14567 không? | Không; đó là port phía broker. Socket client dùng source port riêng. |
| UDP connect thành công đã có QUIC chưa? | Chưa; QUIC phải trao đổi handshake packets. |
| QUIC handshake xong đã publish được ngay chưa? | Code còn cần mở stream và nhận MQTT CONNACK trước. |
| MQTT CONNECT có thay QUIC handshake không? | Không; nó mở session MQTT trên transport đã sẵn sàng. |
| Vì sao stream bidirectional khi subscriber chủ yếu nhận? | Subscriber còn gửi CONNECT, SUBSCRIBE, PINGREQ, DISCONNECT. |
| SUBACK ID 1 có phải stream ID 1? | Không; đó là MQTT packet identifier tương ứng SUBSCRIBE. |
| Mỗi UDP packet có đúng một MQTT message không? | Không; có chia/gộp ở các tầng. |
| Một QUIC stream callback có đúng một MQTT packet không? | Không; parser phải dựa vào Remaining Length. |
| Ai truyền lại dữ liệu khi mất UDP packet? | ngtcp2 quản lý loss detection và QUIC retransmission. |
| Ai chọn subscriber nhận topic? | EMQX. |
| Library ngtcp2 có mã hóa MQTT không? | Không; mqtt_codec mã hóa MQTT, ngtcp2 vận chuyển byte stream. |
| --count 0 có phải không subscribe gì không? | Không; subscriber nhận liên tục tới khi bị dừng. |

Khi bạn có thể vẽ lại hai call chain gửi/nhận và giải thích các counter
queued/sent/acked mà không nhìn source, bạn đã nắm được phần khung quan
trọng nhất để tiếp tục sửa hoặc mở rộng chương trình này.
