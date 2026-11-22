# MQTT 3.1.1 over QUIC với ngtcp2, EMQX và Mininet

Project dùng hai executable C độc lập: `clients/publisher` và `clients/subscriber`.
Broker là Docker image `emqx/emqx-enterprise:6.3.2`. Mọi MQTT packet đi qua
ngtcp2 + GnuTLS, QUIC v1, TLS 1.3, ALPN `mqtt`. Client xác thực CA và tên
`broker.test`, dù địa chỉ kết nối là IP. Không có fallback TCP.

Học source theo luồng thực thi tại
[Hướng dẫn học code chi tiết](docs/HUONG_DAN_HOC_CODE.md).
`clients/subscriber.c` chứa logic SUBSCRIBE/SUBACK, nhận PUBLISH và keepalive;
`clients/publisher.c` chứa logic gửi PUBLISH và chờ QUIC ACK. `app.c` giữ CLI,
CONNECT/CONNACK, nối parser với callbacks và shutdown chung.

**Trạng thái kiểm chứng:** đã build và chạy MQTT over QUIC end-to-end từ h1
đến h2 qua OVS internal port và EMQX/Docker, nhận đúng `Temperature = 28.5 C`.
Bài Mininet chạy bằng sudo trên subnet **`10.78.0.0/24`**, vì subnet gốc
`10.77.0.0/24` đang thuộc `virbr-qdata`. Topology đã cleanup sau test.
Xem [kết quả kiểm thử](docs/KET_QUA_KIEM_THU.md) và [đề xuất subnet](DE_XUAT.md).

## 1. Thành phần

```text
h1 publisher 10.77.0.11 ──┐
                         s1 (standalone) ── mn-gw 10.77.0.254
h2 subscriber 10.77.0.12 ─┘                         │ Ubuntu IP stack
                                                   │ Docker published UDP 14567
                                                   EMQX (Docker bridge)
```

`mn-gw` là OVS internal port trong network namespace của Ubuntu, không phải
interface trong container. Topology không tạo veth nối thẳng Docker, không
tạo NAT riêng, không đổi default route Ubuntu và không xóa mạng có sẵn.
Hai host chỉ cần route kết nối trực tiếp trong cùng `/24` tới gateway.

```text
docker/compose.yaml       Docker ports, named data/log volumes, healthcheck
docker/emqx.conf          QUIC listener và TLS theo schema thật của image 6.3.2
docker/certs/             CA và certificate sinh tại máy, không đưa vào git
mininet/topology.py       Topology, kiểm tra IP, CLI hoặc test h1/h2 tự động
scripts/preflight.py      Kiểm tra xung đột address/route/Docker IPAM (read-only)
scripts/gen-certs.sh      Tạo CA + certificate DNS SAN broker.test
scripts/broker.sh         Kiểm tra quyền sở hữu container trước khi thay đổi
clients/quic_client.*     UDP + QUIC + TLS + một bidirectional stream
clients/mqtt_codec.*      MQTT wire format và bộ phân tích stream tăng dần
clients/app.*             Session chung, CLI, CONNECT/CONNACK, callback dispatch
clients/publisher.c       Publisher: PUBLISH, chờ QUIC ACK, kết thúc
clients/subscriber.c      Subscriber: SUBSCRIBE/SUBACK/PUBLISH, keepalive
tests/                    Codec, network guards, OVS lifecycle, broker integration
```

## 2. Build bằng thư viện hiện có

Chạy từ thư mục project này:

```bash
make
make test
```

Makefile mặc định tìm prefix `../demo/src/.deps`; đã kiểm tra tại máy này:
ngtcp2 **1.25.0**, ngtcp2 crypto GnuTLS **1.25.0**, GnuTLS **3.8.12**.
Không cài lại hoặc build lại thư viện, không sửa project `demo`.
Event loop dùng `poll()` và QUIC timers, nên không cần link libev.
Nếu dùng prefix khác:

```bash
make -C clients DEPS_PREFIX=/duong/dan/toi/prefix
```

Executable có rpath tới thư viện của prefix đã chọn, chạy trong host Mininet
không cần truyền `LD_LIBRARY_PATH` qua sudo. Cần `cc`, `make`, `pkg-config`,
GnuTLS development headers; Mininet sử dụng `/usr/bin/python3`, `mnexec`, OVS,
`iproute2` và `ping`. Chỉ phần tạo topology cần quyền root.

## 3. Docker và certificate

```bash
./scripts/broker.sh up
docker compose -f docker/compose.yaml config
docker compose -f docker/compose.yaml ps
docker exec emqx-quic emqx ctl listeners
```

`broker.sh up` kiểm tra container `emqx-quic` có đúng Compose project
`mqtt-quic-testbed`, service `emqx` và working directory hiện tại; nếu thuộc
project khác thì dừng. Script tạo hoặc dùng lại certificate, kiểm tra Compose,
chờ healthcheck và xác nhận QUIC listener `running : true`.

Client dùng `docker/certs/ca.pem`. Server certificate có DNS SAN `broker.test`.
CA private key không mount vào container; hai private key có mode `0600` và
đều bị gitignore. Image hiện có chạy UID 1000, trùng với user sinh cert tại
máy này. Máy dùng UID khác cần bảo đảm broker đọc được riêng `broker.key`;
không đổi private key thành world-readable.

EMQX 6.3.2 đặt certificate trong `listeners.quic.default.ssl_options` theo
`/opt/emqx/etc/examples/listeners.quic.conf.example` trong image. Client luôn
xác minh server. `verify_none` phía **broker** chỉ nói rằng lab không yêu cầu
client certificate (không dùng mTLS).

Node name cố định là `emqx@127.0.0.1` cho broker một node. Biến
`EMQX_NODE__NAME` ngăn entrypoint thay node name bằng container IP. Data/log
nằm trên named volumes. UDP publish `0.0.0.0:14567:14567/udp`; dashboard ở
<http://127.0.0.1:18083>. Không publish 1883, và các MQTT listener TCP/SSL/WS
khác đã tắt. Lab dùng MQTT client không username/password; certificate vẫn
được xác thực đầy đủ.

Các lệnh quản lý chỉ project này:

```bash
./scripts/broker.sh listeners
./scripts/broker.sh logs
./scripts/broker.sh stop
# Khi muốn bỏ container/network của project; vẫn giữ data/log volumes:
./scripts/broker.sh down
```

## 4. Kiểm thử host qua cổng Docker công bố

```bash
python3 tests/integration.py
```

Test chạy subscriber, chờ CONNACK + SUBACK, nhận PINGRESP thật, rồi chạy
publisher. Nó xác nhận đúng một dòng topic/payload, publisher đã nhận QUIC ACK
cho application bytes, hai process thoát 0, từ chối hostname/CA sai và subscriber
thoát có DISCONNECT khi nhận SIGINT. Logs và JSON report nằm ở `work/host-test/`.
Test không tạo broker giả, không truyền message giữa process bằng file/pipe;
file log chỉ dùng để quan sát readiness và kết quả.

Đây là bài kiểm tra riêng trên host. Bài Mininet ở phần 5 đã được chạy thêm
và PASS trên subnet `10.78.0.0/24`. Có thể chạy tay trên host bằng hai terminal:

```bash
# Terminal 1: chờ dòng MQTT SUBACK accepted
./clients/subscriber --host 127.0.0.1

# Terminal 2
./clients/publisher --host 127.0.0.1
```

Payload mặc định `Temperature = 28.5 C`, topic `lab/temperature`.
Subscriber mặc định chạy tới Ctrl+C; `--count 1` thoát sau message đầu tiên.
Cả hai hỗ trợ `--ca-file`, `--server-name`, `--topic`, `--client-id`, `--timeout`
và `--verbose`; publisher thêm `--message`. Dùng `--help` để xem cú pháp.
Client nhận endpoint IPv4 dạng số; SNI và tên xác thực TLS là tùy chọn riêng.

## 5. Mininet

Kiểm tra trước khi tạo interface:

```bash
python3 scripts/preflight.py --json
python3 mininet/topology.py --check-only
```

Hai lệnh trên hiện trả lỗi có chủ đích vì bridge VM `virbr-qdata` đang dùng
`10.77.0.0/24`. Mặc định vẫn giữ đúng địa chỉ yêu cầu. Không gán thêm cùng
subnet vào `mn-gw` khi route đang thuộc bridge đó.

Đề xuất dùng subnet thay thế đã kiểm tra không xung đột (kiểm tra lại mỗi lần):

```bash
python3 scripts/preflight.py --subnet 10.78.0.0/24 --json
sudo /usr/bin/python3 mininet/topology.py --subnet 10.78.0.0/24 --test
```

Lúc này h1=`10.78.0.11`, h2=`10.78.0.12`, mn-gw/endpoint=`10.78.0.254`.
Script tự truyền endpoint này cho hai client, vẫn xác thực `broker.test`.
Test chạy ping h1→gateway, h2→gateway, h1→h2, chờ SUBACK trên h2 rồi publish
từ h1, kiểm tra topic/payload và exit codes. Log mặc định `work/mininet-*`.
Bài này đã **PASS**; log kiểm chứng tại `work/mininet-sudo-e2e/` và
`work/mininet-sudo-e2e.log`. Publisher/subscriber đều exit 0. OVS s1/mn-gw,
các veth của lab và route `10.78.0.0/24` đã được dọn sau khi kết thúc.

Nếu subnet mặc định đã được giải phóng bằng thao tác quản trị phù hợp,
không cần `--subnet`. Để mở Mininet CLI:

```bash
sudo /usr/bin/python3 mininet/topology.py --subnet 10.78.0.0/24
```

CLI in sẵn lệnh chạy hai client với đường dẫn tuyệt đối và endpoint đang dùng.
Chạy subscriber trên h2 trước, xem log tới `MQTT SUBACK accepted`, rồi chạy
publisher trên h1. Các lệnh ping khi dùng subnet mặc định:

```text
mininet> h1 ping -c 3 10.77.0.254
mininet> h2 ping -c 3 10.77.0.254
mininet> h1 ping -c 3 h2
```

Topology khóa tên tài nguyên trong suốt vòng đời, từ chối bridge/interface đã
tồn tại và gắn UUID ownership vào s1/mn-gw. `finally` chỉ xóa tài nguyên đã
tạo và vẫn mang đúng UUID. Không chạy `mn -c` hoặc Docker prune.

## 6. QUIC stream và MQTT

Mỗi process tạo UDP socket riêng. ngtcp2 tạo/đọc QUIC packets, quản lý
connection IDs, ACK, retransmission, congestion/flow control và expiry timer.
GnuTLS xử lý TLS 1.3 cùng CA/SNI/ALPN verification. Chỉ sau handshake được
xác minh, client mở một bidirectional stream (stream 0) và gửi MQTT CONNECT.

Trên stream đó là chuỗi byte của các MQTT control packets. QUIC trả dữ liệu
đúng thứ tự, nhưng không giữ ranh giới MQTT packet. `mqtt_codec` giữ dữ liệu
chưa đủ, đọc Remaining Length (tối đa bốn byte), rồi xử lý được cả packet bị
chia nhỏ lẫn nhiều packet gộp trong một callback. Parser giới hạn memory cho
một packet ở 1 MiB, kiểm tra flags, UTF-8, topic length và packet identifier.
Lab chỉ nhận/gửi PUBLISH QoS 0, subscribe đúng một topic QoS 0.

Publisher đợi CONNACK, gửi PUBLISH, đợi **QUIC ACK cho toàn bộ application
bytes**, rồi DISCONNECT. QoS 0 không có MQTT PUBACK: chỉ subscriber nhận đúng
payload mới chứng minh publish/subscribe end-to-end. Subscriber chờ SUBACK,
duy trì MQTT keepalive 20 giây bằng PINGREQ khi im lặng 10 giây. Shutdown
giữ buffer tới ACK, drain DISCONNECT, gửi FIN, chờ FIN ACK, rồi QUIC CLOSE.

## 7. Chẩn đoán theo tầng

| Bằng chứng | Tầng đã kiểm chứng |
|---|---|
| ping thành công | IP/ICMP |
| `QUIC_HANDSHAKE_OK ... certificate=verified` | QUIC + TLS 1.3 + ALPN mqtt |
| `MQTT CONNACK accepted` | MQTT CONNECT |
| `MQTT SUBACK accepted` | MQTT subscription |
| Dòng `MQTT PUBLISH received ...` đúng topic/payload | Message qua broker |
| `QUIC_DRAIN_OK` và exit 0 | Application bytes/FIN được ACK, shutdown thành công |

Nếu IP thông nhưng QUIC timeout, quan sát trước khi sửa firewall:

```bash
sudo tcpdump -ni mn-gw udp port 14567
docker exec emqx-quic emqx ctl listeners
./scripts/broker.sh logs
sudo iptables -nvL DOCKER-USER
sudo iptables -t nat -S DOCKER
sudo nft list ruleset
```

Chưa có rule firewall nào bị sửa trong lần triển khai này. Chỉ thêm rule với
phạm vi interface/subnet/UDP port cần thiết khi có bằng chứng firewall chặn;
không flush ruleset. Nếu `TLS_CERTIFICATE_REJECTED`, kiểm tra CA, SAN, tên
`broker.test` và thời gian chứng chỉ, không tắt verification.

Nguồn đối chiếu: [MQTT 3.1.1 (OASIS)](https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/mqtt-v3.1.1.html),
[EMQX MQTT over QUIC](https://docs.emqx.com/en/emqx/latest/develop/mqtt-over-quic/getting-started.html),
[ngtcp2 API](https://nghttp2.org/ngtcp2/).
API biên dịch thực tế được kiểm tra thêm bằng headers/source ngtcp2 1.25.0
đang có trong project và cấu hình mẫu của Docker image 6.3.2.
