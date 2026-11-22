# Kết quả triển khai MQTT over QUIC

Ngày kiểm tra: **09/11/2026, Asia/Bangkok**.

Cập nhật sau khi tách source publisher/subscriber: build không có warning,
codec + 9 Python tests PASS; cả 4 host integration cases PASS tại
`work/host-test-separated-clients/report.json`; bài h1/h2 Mininet dùng sudo
PASS lại tại `work/mininet-separated-clients.log` và
`work/mininet-separated-clients/`. Payload giữ đúng `Temperature = 28.5 C`,
hai client exit 0. Hướng dẫn học bản source mới nằm tại
`docs/HUONG_DAN_HOC_CODE.md`.

Source được viết tại:
`/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic`.
Các thay đổi có sẵn trong `demo` và `third_party` được giữ nguyên.

**Kết luận: PASS MQTT over QUIC end-to-end trên Mininet h1/h2**, qua OVS
internal port `mn-gw`, Docker published UDP port và EMQX 6.3.2. Hai process C
dùng ngtcp2 nhận đúng payload và thoát mã 0. Bài test chạy bằng sudo trong
phạm vi project trên subnet thay thế **`10.78.0.0/24`**; subnet gốc vẫn được
giữ cho VM đang dùng `virbr-qdata`.

## Môi trường và trạng thái

| Thành phần | Kết quả thực tế |
|---|---|
| Ubuntu | 26.04.1 LTS |
| ngtcp2 / ngtcp2 crypto GnuTLS | 1.25.0 / 1.25.0, dùng lại `../demo/src/.deps` |
| GnuTLS | 3.8.12 |
| Docker image | `emqx/emqx-enterprise:6.3.2`, dùng image đã có |
| Container | `emqx-quic`, healthy; được giữ chạy để tiếp tục thử |
| Node | `emqx@127.0.0.1` |
| QUIC listener | `quic:default`, `0.0.0.0:14567`, `running : true` |
| Port công bố | `0.0.0.0:14567/udp`, dashboard `127.0.0.1:18083/tcp` |
| Docker bridge riêng của project | `172.19.0.0/16` tại thời điểm chạy |
| Chứng chỉ | CA local, DNS SAN `broker.test`; client kiểm tra CA + tên server |
| Firewall/default route Ubuntu | Không thay đổi |
| Mạng VM có sẵn | `virbr-qdata` vẫn giữ `10.77.0.1/24` |
| OVS internal port `mn-gw` | Đã tạo `10.78.0.254/24`, chạy test, cleanup thành công |
| h1 / h2 | `10.78.0.11/24` / `10.78.0.12/24` trong hai Mininet hosts |

Trước triển khai không có container `emqx-quic`; container khác được giữ
nguyên. Cấu hình QUIC được đối chiếu với file mẫu trong image 6.3.2 và kiểm
tra listener thực tế, không chỉ dựa trên tài liệu phiên bản latest.

## Kiểm thử đã chạy

| Kiểm tra | Kết quả |
|---|---|
| `docker compose -f docker/compose.yaml config` | PASS |
| Compose up, ps, EMQX ctl status/listeners | PASS, healthy và QUIC running |
| Build hai executable C bằng ngtcp2 hiện có | PASS, không compiler warnings |
| MQTT codec: wire vectors, Remaining Length, chia/gộp packet, input sai | PASS |
| 9 Python tests: subnet guards và Mininet/OVS lifecycle | PASS |
| Python syntax, bash syntax | PASS |
| TLS 1.3, ALPN mqtt, CA/SNI đúng | PASS với broker thật |
| MQTT CONNACK, SUBACK và đúng topic/payload | PASS với hai process C độc lập |
| PINGREQ/PINGRESP khi subscriber chờ | PASS |
| Hostname sai `wrong.test` | TLS từ chối, exit 1, không tới CONNACK |
| CA không tin cậy độc lập | TLS từ chối, exit 1, không tới CONNACK |
| Subscriber nhận SIGINT | DISCONNECT, QUIC drain, exit 0 |
| Payload 32 KiB qua broker QUIC | PASS, so khớp toàn bộ payload |
| Default subnet collision | Phát hiện đúng `virbr-qdata`, dừng trước khi tạo mạng |
| Subnet thay thế `10.78.0.0/24` | Preflight CLEAR tại thời điểm kiểm tra |
| Mininet `--test` trên subnet thay thế, dùng sudo | PASS, exit 0 |
| Ping h1→mn-gw, h2→mn-gw, h1→h2 | Mỗi lượt 3/3 packet, mất gói 0% |
| QUIC/TLS, CONNACK, SUBACK trên h1/h2 | PASS |
| h1 publisher → EMQX → h2 subscriber | Đúng topic và payload, hai process exit 0 |
| Cleanup sau Mininet | s1/mn-gw/veth/route lab không còn; VM/default routes giữ nguyên |

Hai lifecycle tests dùng dispatch `Mininet.start/stop` đang cài, mock các
thao tác tạo shell/interface và OVS. Chúng kiểm tra đường gọi start/cleanup;
không thay thế cho bài kiểm thử network namespace thực tế.

Bài integration cuối chạy **03:56:16–03:56:27 UTC+07:00**, cả 4 trường hợp
đều PASS. Báo cáo máy tại `work/host-test/report.json`; các log liên quan nằm
cùng thư mục. Một lần chạy trước bị một publication kiểm thử khác trên cùng
topic làm gián đoạn và không được tính PASS; lưu riêng tại
`work/host-test-concurrent-attempt/`. Lần cuối chạy tuần tự không có publication
chen vào. Log thử payload lớn nằm ở `work/large-payload/`.

Trích log subscriber ở lần integration cuối:

```text
QUIC_HANDSHAKE_OK TLS1.3 ALPN=mqtt server_name=broker.test certificate=verified
QUIC_STREAM_READY id=0 bidirectional=1
MQTT CONNACK accepted
MQTT SUBACK accepted
MQTT PINGREQ sent
MQTT PINGRESP received
MQTT PUBLISH received topic=lab/temperature payload=Temperature = 28.5 C
MQTT DISCONNECT queued
QUIC_DRAIN_OK acked=62
```

Publisher có `MQTT PUBLISH acknowledged by QUIC`, `QUIC_DRAIN_OK acked=76`
và exit 0. Dữ liệu đi qua hai QUIC connections và EMQX; log/file chỉ quan sát
readiness/kết quả, không truyền message giữa publisher và subscriber.

## Kiểm thử Mininet bằng sudo

Sau khi được cấp quyền dùng sudo riêng cho dự án, đã chạy:

```bash
sudo /usr/bin/python3 -u mininet/topology.py \
  --subnet 10.78.0.0/24 --test --log-dir work/mininet-sudo-e2e
```

Kết quả thực tế lúc khoảng **04:01 UTC+07:00, ngày 09/11/2026**:

```text
h1=10.78.0.11/24 h2=10.78.0.12/24 mn-gw=10.78.0.254/24 EMQX=10.78.0.254:14567/UDP
IP connectivity: PASS. QUIC/TLS and MQTT require the client test.
MQTT SUBSCRIBE: SUBACK accepted on h2
QUIC_HANDSHAKE_OK TLS1.3 ALPN=mqtt server_name=broker.test certificate=verified
MQTT CONNACK accepted
MQTT SUBACK accepted
MQTT PUBLISH received topic=lab/temperature payload=Temperature = 28.5 C
MQTT DISCONNECT queued
QUIC_DRAIN_OK acked=60
PASS: h1 -> QUIC/ngtcp2 -> EMQX/Docker -> QUIC/ngtcp2 -> h2; topic and payload verified.
*** Done
```

Publisher trên h1 có `MQTT PUBLISH acknowledged by QUIC`,
`QUIC_DRAIN_OK acked=76` và exit 0. Test chỉ in PASS khi subscriber cũng exit
0 và có đúng topic/payload. Console log tại `work/mininet-sudo-e2e.log`;
hai client logs tại `work/mininet-sudo-e2e/`. Không cần sửa firewall.

Kiểm tra sau khi thoát xác nhận: không còn s1/mn-gw trong OVS DB; không còn
interface/veth/route `10.78.0.0/24` của topology; không còn process client
hoặc mnexec của test. `virbr-qdata` và default routes vẫn giữ nguyên. EMQX
tiếp tục healthy, QUIC listener `running : true`, current connections = 0.
Mật khẩu không được lưu trong source, script hoặc báo cáo.

Subnet mặc định `10.77.0.0/24` vẫn bị preflight từ chối đúng do VM đang sử
dụng. Việc dùng `10.78.0.0/24` được ghi riêng trong `DE_XUAT.md`.
Để chạy lại tại thư mục source nói trên:

```bash
make
make test
./scripts/broker.sh up
python3 scripts/preflight.py --subnet 10.78.0.0/24 --json
sudo /usr/bin/python3 mininet/topology.py --subnet 10.78.0.0/24 --test
```

Script dùng h1 `10.78.0.11`, h2 `10.78.0.12`, mn-gw `10.78.0.254`; tự kiểm
tra IP, chờ SUBACK trên h2 rồi publish từ h1. Lệnh cuối đã PASS và xác nhận
đường đi h1 → s1 → mn-gw → Docker/EMQX → h2. Nếu bắt buộc giữ subnet gốc,
cần xử lý xung đột mạng VM trước rồi bỏ tham số `--subnet`.

Tóm tắt cách hoạt động: mỗi process có một QUIC connection và mở một
bidirectional stream sau TLS verification. MQTT CONNECT/SUBSCRIBE/PUBLISH
là các packet nối tiếp trên stream; codec tái ghép theo Remaining Length.
Publisher chờ QUIC ACK trước khi shutdown, còn việc subscriber nhận đúng
payload là bằng chứng trao đổi MQTT qua broker.
