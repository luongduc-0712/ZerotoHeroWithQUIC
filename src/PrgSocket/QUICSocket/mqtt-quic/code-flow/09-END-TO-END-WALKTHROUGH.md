# Theo dõi một lần chạy end-to-end như debugger

## 1. Tình huống và giới hạn của bản diễn giải

Chọn đường triển khai có thật: `topology.py --subnet 10.78.0.0/24 --test`, publisher trên h1 gửi `Temperature = 28.5 C` tới `lab/temperature`, subscriber trên h2 nhận một message rồi đóng. Endpoint, topic, message và `--count 1` được dựng trong [mininet/topology.py:127-133](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:127); trình tự launch/đối chiếu kết quả ở [mininet/topology.py:168-206](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:168).

**[SOURCE]** Các bước dưới là một interleaving hợp lệ theo điều kiện của code; không phải bản ghi CPU/pcap mới. Dữ liệu, điều kiện chuyển nhánh và call sites lấy từ source. **[HISTORICAL]** Log của đường chạy này đã có ở [work/mininet-separated-clients.log:8-55](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/work/mininet-separated-clients.log:8) cùng hai client logs. **[UNVERIFIED]** Không gán PID, ephemeral port, CID, packet number, thời gian RTT hoặc IP/MAC container cụ thể cho lần chạy hiện tại.

Khi qua broker, ta đánh dấu **ranh giới binary bên ngoài**: có config/contract và bằng chứng message tới subscriber, nhưng không có source EMQX image tại đây để đặt tên hàm xử lý nội bộ. Các bước Linux/OVS/Docker ghi [THEORY]/[CONFIG] tương ứng và không được xem như lời gọi hàm C xuyên process.

## 2. Lệnh chọn cho kịch bản

Nhập ở Ubuntu, từng bước sau khi bước trước thành công:

```bash
cd /home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic
make
/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/scripts/broker.sh up
sudo /usr/bin/python3 /home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py --subnet 10.78.0.0/24 --test
```

Đây là hướng dẫn để người đọc thực nghiệm, **không phải các lệnh đã được chạy trong phiên viết tài liệu**. Nếu `mn-gw` đang tồn tại, phải xác định phiên cũ và đóng đúng phiên trước; không mở chồng. Preflight nằm trước cả yêu cầu root và khóa nên một phiên đang sống có thể làm lần gọi mới báo subnet conflict trước khi tới thông báo lock. [mininet/topology.py:250-259](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:250).

## 3. Các ký hiệu dữ liệu sẽ dùng

| Ký hiệu | Nguồn và ý nghĩa | Không được suy ra |
|---|---|---|
| `c_s`, `app_s` | Đối tượng transport/app trong subscriber | Không cùng địa chỉ với publisher |
| `c_p`, `app_p` | Đối tượng transport/app trong publisher | Không phải object broker |
| `P_s`, `P_p` | Source UDP port do kernel chọn khi UDP connect | Không cố định 14567 |
| `n_s`, `n_p` | Độ dài client ID thực tế của hai process | Không hardcode PID hoặc số chữ số PID |
| `t_ns`, `t_ms` | CLOCK_MONOTONIC đổi sang nanosecond/millisecond | Không phải thời gian UTC trong packet |
| `Q`, `S`, `A` | tx_queued, tx_sent, tx_acked của một connection | Không là số UDP packet |

Nguồn: [clients/quic_client.c:24-61](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:24), [clients/quic_client.c:212-236](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:212), [clients/app.c:120-125](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:120). Các địa chỉ c/app là nhãn để theo debugger; code thực tế dùng biến tên `c`, `app` trong từng process riêng.

## 4. Byte MQTT được tính từ source

Payload CLI là chuỗi ASCII 20 byte; topic dài 15 byte. `mqtt_encode_publish` dùng `strlen(app.message)` nên byte NUL kết thúc chuỗi C không được gửi. [clients/publisher.c:14-16](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/publisher.c:14), [clients/mqtt_codec.c:49-54](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:49).

```text
PUBLISH QoS 0:
30 25 00 0F
6C 61 62 2F 74 65 6D 70 65 72 61 74 75 72 65
54 65 6D 70 65 72 61 74 75 72 65 20 3D 20 32 38 2E 35 20 43
```

- `30`: type 3, flags 0.
- `25`: Remaining Length 37 = 2 byte topic-length + 15 byte topic + 20 byte payload.
- `00 0F`: topic length 15, ghi bằng put16; **khác** encoding base128 của Remaining Length.
- Tổng packet = 1+1+37 = **39 byte MQTT**, không phải kích thước QUIC/UDP/IP/Ethernet packet.

Phép tính byte bám [clients/mqtt_codec.c:5-6](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:5), [clients/mqtt_codec.c:30-36](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:30), [clients/mqtt_codec.c:49-54](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:49). Packet boundary không được QUIC bảo toàn; 39 byte có thể được chia/gộp với MQTT bytes khác qua các lần callback. [clients/mqtt_codec.c:78-98](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:78).

| Packet trong kịch bản | Byte/kích thước MQTT | Nguồn |
|---|---|---|
| CONNECT | `10 RL [00 04 MQTT 04 02 00 14] [id_len_16] [id]`; mặc định ngắn: 14+n byte | [clients/mqtt_codec.c:38-43](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:38) |
| CONNACK chấp nhận | `20 02 00 00`, 4 byte theo decoder app chấp nhận | [clients/mqtt_codec.c:99-102](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:99), [clients/app.c:51-61](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:51) |
| SUBSCRIBE | `82 14 00 01 00 0F [topic15] 00`, 22 byte | [clients/subscriber.c:25-26](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/subscriber.c:25), [clients/mqtt_codec.c:44-48](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:44) |
| SUBACK chấp nhận | `90 03 00 01 00`, 5 byte | [clients/mqtt_codec.c:103-106](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:103), [clients/subscriber.c:37-47](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/subscriber.c:37) |
| PUBLISH | 39 byte như trên | [clients/mqtt_codec.c:49-54](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:49) |
| PINGREQ/PINGRESP | `C0 00` / `D0 00`, mỗi packet 2 byte, chỉ nếu đủ điều kiện thời gian | [clients/mqtt_codec.c:55](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:55), [clients/subscriber.c:73-108](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/subscriber.c:73) |
| DISCONNECT | `E0 00`, 2 byte | [clients/mqtt_codec.c:56](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:56), [clients/app.c:24-34](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:24) |

CONNECT/SUBSCRIBE/PUBLISH là application bytes; QUIC ACK không là MQTT packet trong bảng. Header/crypto tag/ACK của QUIC do dependency tạo ngoài các byte này.

## 5. Sequence xuyên process, có ranh giới broker

```mermaid
sequenceDiagram
  participant U as Shell Ubuntu
  participant M as topology.py / Mininet
  participant S as Subscriber h2
  participant B as EMQX / Docker (ngoài source C)
  participant P as Publisher h1
  U->>B: broker.sh up / Compose (CONFIG)
  U->>M: topology.py --subnet .78/24 --test
  M->>M: preflight, lock, hosts/links/s1/mn-gw, ping
  M->>S: h2.popen(subscriber, --count 1)
  S->>B: QUIC/TLS handshake connection S
  S->>B: CONNECT trên stream S
  B-->>S: CONNACK
  S->>B: SUBSCRIBE topic
  B-->>S: SUBACK
  S->>S: state=SUBSCRIBED; in marker
  M->>M: wait_subscribed thấy marker
  M->>P: h1.popen(publisher, --message ...)
  P->>B: QUIC/TLS handshake connection P
  P->>B: CONNECT trên stream P
  B-->>P: CONNACK
  P->>B: PUBLISH MQTT 39 byte qua QUIC P
  Note over B: Nội bộ nhận/routing MQTT không có source trong project
  par Transport xác nhận connection P
    B-->>P: QUIC ACK stream bytes
    P->>B: DISCONNECT, FIN, đóng QUIC
  and Broker chuyển message trên connection S
    B->>S: PUBLISH cùng topic/payload qua QUIC S
    S->>S: decode, fwrite, received=1
    S->>B: DISCONNECT, FIN, đóng QUIC
  end
  P-->>M: exit status
  S-->>M: exit status
  M->>M: đối chiếu logs; PASS; scoped cleanup
```

Hai nhánh sau broker có thể xen kẽ theo scheduling và mạng; source không đảm bảo log ACK phía publisher xuất hiện trước hay sau log payload phía subscriber. Chỉ gate SUBACK→launch publisher được orchestration cưỡng chế. [mininet/topology.py:184-203](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:184), [clients/publisher.c:29-39](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/publisher.c:29), [clients/subscriber.c:51-70](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/subscriber.c:51).

## 6. Nhật ký từng bước có nguồn tham số và điều kiện chuyển tiếp
### Bước 01 — Build mã nguồn trước khi có client process

1. **Thứ tự / process:** 01; Shell Ubuntu / make / compiler.
2. **Hàm/điểm thực thi:** `Makefile all → make -C clients`.
3. **File/dòng:** [Makefile:1-6](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/Makefile:1); [clients/Makefile:1-35](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/Makefile:1).
4. **Đầu vào và nguồn:** Chưa có argv của client ở bước này. Compiler nhận C11 flags và include/libs từ pkg-config tại prefix demo/src/.deps; linker ghép object của role với các object app/codec/quic dùng chung.
5. **Lệnh/khối quan trọng:** Biên dịch .c thành .o rồi liên kết thành hai executable, có rpath tới prefix thư viện. Đây chưa phải lúc thực thi main của publisher/subscriber.
6. **Dữ liệu/trạng thái sau bước:** Có các file binary; chưa có app, c hoặc UDP socket của lần demo mới.
7. **Hàm/sự kiện tiếp:** Chạy broker.sh up.
8. **Lý do chuyển tiếp:** Cần chuẩn bị listener và certificate của broker trước khi client kết nối. Target make mặc định không mở broker; target broker là lựa chọn riêng.

### Bước 02 — Broker owner check và certificate

1. **Thứ tự / process:** 02; Shell Ubuntu / Docker CLI / openssl.
2. **Hàm/điểm thực thi:** `broker.sh case up → check_owner → gen-certs.sh`.
3. **File/dòng:** [scripts/broker.sh:1-36](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/scripts/broker.sh:1); [scripts/gen-certs.sh:1-42](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/scripts/gen-certs.sh:1).
4. **Đầu vào và nguồn:** ROOT được tính từ đường dẫn script; PROJECT = mqtt-quic-testbed, COMPOSE_DIR = ROOT/docker; container có tên emqx-quic. Các certificate được đặt dưới docker/certs.
5. **Lệnh/khối quan trọng:** Nếu container đã tồn tại, đối chiếu labels project/service/working_dir. Bộ certificate đầy đủ thì verify và dùng lại; bộ thiếu một phần thì lỗi; nếu chưa có bộ nào thì tạo CA/certificate có SAN broker.test.
6. **Dữ liệu/trạng thái sau bước:** Certificate đã được chuẩn bị. Container không thuộc đúng project/working directory bị chặn trước thao tác thay đổi.
7. **Hàm/sự kiện tiếp:** Compose kiểm config, up và wait.
8. **Lý do chuyển tiếp:** Shell dùng set -e: chỉ khi owner check và script certificate thành công mới thực hiện phần tiếp theo.

### Bước 03 — Tạo/chờ container và kiểm listener

1. **Thứ tự / process:** 03; Docker daemon/container EMQX ngoài source project.
2. **Hàm/điểm thực thi:** `compose up -d --wait; docker exec emqx-quic emqx ctl listeners`.
3. **File/dòng:** [scripts/broker.sh:24-36](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/scripts/broker.sh:24); [docker/compose.yaml:1-34](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/docker/compose.yaml:1); [docker/emqx.conf:12-33](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/docker/emqx.conf:12).
4. **Đầu vào và nguồn:** Image là emqx/emqx-enterprise:6.3.2; publish UDP trên 0.0.0.0:14567, mount config/certificate; node name cố định và healthcheck lấy từ Compose.
5. **Lệnh/khối quan trọng:** Broker script đợi healthcheck với --wait-timeout 90 rồi kiểm phần quic:default có running = true. [CONFIG] Docker network broker dùng bridge driver.
6. **Dữ liệu/trạng thái sau bước:** Khi người đọc chạy lệnh và nhận thành công, startup có kiểm tra listener tại thời điểm đó. Tài liệu không tự khởi động hoặc xác minh lại broker trong lượt viết.
7. **Hàm/sự kiện tiếp:** Chạy main của topology.
8. **Lý do chuyển tiếp:** Container healthy chưa phải toàn bộ tiêu chí: script còn kiểm tra listener QUIC trước khi trả thành công.

### Bước 04 — Parse subnet và kiểm tra không trùng

1. **Thứ tự / process:** 04; Process Python topology.py trong host Ubuntu.
2. **Hàm/điểm thực thi:** `main → options → require_clear`.
3. **File/dòng:** [mininet/topology.py:225-259](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:225); [scripts/preflight.py:17-91](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/scripts/preflight.py:17).
4. **Đầu vào và nguồn:** argv có --subnet 10.78.0.0/24 --test. options yêu cầu IPv4Network private /24 đúng network boundary; timeout Python mặc định là 25 giây; ca_file được resolve thành đường dẫn tuyệt đối.
5. **Lệnh/khối quan trọng:** Đọc IPv4 address, route trong mọi bảng và Docker IPAM; bỏ default route khi so sánh subnet rồi kiểm overlap. Có xung đột thì raise RuntimeError trước khi tạo mạng.
6. **Dữ liệu/trạng thái sau bước:** args giữ subnet/path/mode; net = None, lock = None, owner là UUID mới, gateway_created = False.
7. **Hàm/sự kiện tiếp:** Kiểm runtime requirements, lấy khóa và kiểm tên tài nguyên.
8. **Lý do chuyển tiếp:** Chỉ subnet clear mới tới kiểm root/dependencies. Nếu chọn --check-only, script return tại đây và không tạo topology.

### Bước 05 — Khóa vòng đời và kiểm tài nguyên dành riêng

1. **Thứ tự / process:** 05; Python topology.py / file descriptor / OVS CLI.
2. **Hàm/điểm thực thi:** `require_runtime → acquire_lock → require_clear → check_reserved_names`.
3. **File/dòng:** [mininet/topology.py:39-72](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:39); [mininet/topology.py:256-261](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:256).
4. **Đầu vào và nguồn:** Lock file /run/lock/mqtt-quic-mininet.lock được mở với O_NOFOLLOW, rồi kiểm regular file và root owner; dùng LOCK_EX | LOCK_NB. Các tên dành riêng gồm s1, mn-gw, h1-eth0, h2-eth0, s1-eth1, s1-eth2.
5. **Lệnh/khối quan trọng:** Kiểm quyền root và binary phụ thuộc; giữ khóa suốt vòng đời; kiểm lại subnet sau khi có khóa; kiểm OVS và các tên interface.
6. **Dữ liệu/trạng thái sau bước:** Có lock descriptor được giữ tới finally. Script không xóa bridge/interface có sẵn chỉ vì trùng tên.
7. **Hàm/sự kiện tiếp:** `make_network(owner)`.
8. **Lý do chuyển tiếp:** Các guard ngăn hai phiên cùng tạo tài nguyên. Nếu một guard lỗi thì nhánh startup thành công dừng, chuyển qua xử lý lỗi/cleanup phù hợp.

### Bước 06 — Tạo đối tượng Mininet và lớp switch sở hữu

1. **Thứ tự / process:** 06; Python topology.py / dependency Mininet.
2. **Hàm/điểm thực thi:** `make_network(owner)`.
3. **File/dòng:** [mininet/topology.py:75-124](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:75).
4. **Đầu vào và nguồn:** owner là UUID của lần chạy; constructor là Mininet(controller=None, switch=OwnedBridge, build=False, autoSetMacs=True).
5. **Lệnh/khối quan trọng:** Định nghĩa start/stop riêng cho bridge, vô hiệu batch startup/shutdown vốn có thể xóa vô điều kiện; tạo Mininet nhưng chưa build topology. Phần init/tuning của dependency được giải thích ở tài liệu 01.
6. **Dữ liệu/trạng thái sau bước:** Caller đã giữ net trước khi tạo host/link, để finally có object quản lý khi cần cleanup sau lỗi một phần.
7. **Hàm/sự kiện tiếp:** addHost/addSwitch/addLink, sau đó build/start.
8. **Lý do chuyển tiếp:** build=False tách việc có object quản lý khỏi việc tạo tài nguyên; nhờ đó có thể thu hồi phần đã tạo khi startup thất bại.

### Bước 07 — Tạo h1/h2, veth và bridge s1

1. **Thứ tự / process:** 07; Python Mininet + kernel namespaces / OVS.
2. **Hàm/điểm thực thi:** `net.addHost/addSwitch/addLink; net.build; net.start → OwnedBridge.start`.
3. **File/dòng:** [mininet/topology.py:261-270](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:261); [mininet/topology.py:97-107](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:97).
4. **Đầu vào và nguồn:** h1 có IP 10.78.0.11/24, h2 là 10.78.0.12/24, tính từ network_address + 11/+12; switch tên s1. Dùng link mặc định Mininet, không chọn TCLink.
5. **Lệnh/khối quan trọng:** Dependency tạo host shell/network namespace và các cặp veth. OwnedBridge.start tạo s1 bằng giao dịch add-br, đặt standalone/owner UUID và thêm các port dữ liệu.
6. **Dữ liệu/trạng thái sau bước:** Các interface h1/h2 và liên kết tới s1 đã được dựng. Topology không tạo controller hay router node riêng.
7. **Hàm/sự kiện tiếp:** Tạo internal port mn-gw.
8. **Lý do chuyển tiếp:** net.start phải thành công trước khi thêm port gateway. Nếu lỗi, finally chỉ yêu cầu cleanup theo phạm vi và ownership đã được theo dõi.

### Bước 08 — Đặt địa chỉ host endpoint và kiểm IP

1. **Thứ tự / process:** 08; Python/OVS/ip/ping subprocess.
2. **Hàm/điểm thực thi:** `ovs add-port; ip address add; ip link up; check_ip`.
3. **File/dòng:** [mininet/topology.py:271-282](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:271); [mininet/topology.py:209-222](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:209).
4. **Đầu vào và nguồn:** Port mn-gw có type = internal và owner UUID; gateway = network_address + 254 = 10.78.0.254, prefix /24. Kiểm ping h1→gateway, h2→gateway và h1→h2, mỗi lượt 3 gói.
5. **Lệnh/khối quan trọng:** Tạo internal port trong host, gán IP, bật link và kiểm fail_mode = standalone; chạy ba phép ping rồi kiểm return code của từng process.
6. **Dữ liệu/trạng thái sau bước:** gateway_created được bật ngay sau khi tạo port thành công để finally theo dõi cleanup. Chỉ khi các kiểm tra IP thành công mới chạy client test.
7. **Hàm/sự kiện tiếp:** `run_test(h1, h2, args)`.
8. **Lý do chuyển tiếp:** Ping kiểm chứng kết nối IP/ICMP; QUIC, TLS và MQTT còn cần bài kiểm thử ứng dụng ở bước tiếp theo.

### Bước 09 — Launch subscriber trước

1. **Thứ tự / process:** 09; Python topology.py → process subscriber trong h2.
2. **Hàm/điểm thực thi:** `run_test → client_commands → h2.popen`.
3. **File/dòng:** [mininet/topology.py:127-142](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:127); [mininet/topology.py:168-187](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:168).
4. **Đầu vào và nguồn:** Lệnh gồm executable subscriber với --host 10.78.0.254 --port 14567, CA tuyệt đối, --server-name broker.test --topic lab/temperature --count 1. cwd = ROOT; stdout và stderr được ghi vào subscriber.log.
5. **Lệnh/khối quan trọng:** check_clients kiểm binary executable và CA; tạo thư mục log mới hoặc yêu cầu thư mục đã có phải rỗng; h2.popen launch process trong môi trường mạng h2.
6. **Dữ liệu/trạng thái sau bước:** Subscriber là process riêng đã bắt đầu; Python chưa tạo publisher. File log dùng quan sát readiness/kết quả, không làm kênh chuyển message MQTT.
7. **Hàm/sự kiện tiếp:** subscriber.main chạy; trong khi đó Python thực hiện wait_subscribed.
8. **Lý do chuyển tiếp:** Readiness được quyết định bởi marker SUBACK, thay vì chỉ thấy process tồn tại hoặc đợi cố định một khoảng thời gian.

### Bước 10 — Subscriber chuẩn bị role/context

1. **Thứ tự / process:** 10; Process subscriber h2.
2. **Hàm/điểm thực thi:** `main(argc,argv) → mqtt_app_main(argc,argv,&role,&subscriber)`.
3. **File/dòng:** [clients/subscriber.c:113-125](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/subscriber.c:113); [clients/app.h:10-29](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.h:10).
4. **Đầu vào và nguồn:** `argc`/`argv` đến từ lệnh launch. Biến `subscriber_context` trên stack có `state = WAIT_SUBACK` (2), các trường còn lại bằng 0; role có tên subscriber, cờ subscriber bằng 1, `default_timeout = 0` và ba con trỏ hàm.
5. **Lệnh/khối quan trọng:** Gán địa chỉ callback mà chưa thực thi chúng; truyền `role_data = &subscriber` vào session dùng chung.
6. **Dữ liệu/trạng thái sau bước:** Context và role còn sống trên stack của `main()` trong suốt lời gọi đồng bộ `mqtt_app_main()`. Chưa gửi SUBSCRIBE dù state ban đầu mang tên WAIT_SUBACK.
7. **Hàm/sự kiện tiếp:** `mqtt_app_main()` khởi tạo options/app và đọc CLI.
8. **Lý do chuyển tiếp:** Role chọn hành vi sau CONNACK. Gate `app.connected` trong tick ngăn việc dùng deadline SUBACK trước khi subscription thật sự được tạo.

### Bước 11 — Subscriber tạo app và override default bằng CLI

1. **Thứ tự / process:** 11; Process subscriber h2.
2. **Hàm/điểm thực thi:** `mqtt_app_main`.
3. **File/dòng:** [clients/app.c:115-161](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:115).
4. **Đầu vào và nguồn:** Role và role_data đến từ `main()`. Host mặc định `10.77.0.254` được `--host 10.78.0.254` ghi đè; client ID mặc định có dạng `mqtt-subscriber-PID`; topic, CA và count lấy từ lệnh launch.
5. **Lệnh/khối quan trọng:** `getopt_long()` lưu con trỏ chuỗi trong argv, kiểm tra các số port/count/timeout; cấu hình stdout theo dòng, đặt `started = now_ms`, gắn bảng `quic_callbacks`, rồi gọi `quic_client_run(..., &app)`.
6. **Dữ liệu/trạng thái sau bước:** Parser và các trường chưa chỉ định của app_s được zero-init: `connected = 0`, `connect_sent = 0`; role_data trỏ tới context subscriber, count bằng 1. Timeout tổng của C bằng 0; `args.timeout = 25` của Python không được truyền thành C `--timeout`.
7. **Hàm/sự kiện tiếp:** `quic_client_run()` tạo c_s.
8. **Lý do chuyển tiếp:** Chỉ CLI hợp lệ mới chạy transport; options/app/callbacks trên stack vẫn còn sống vì lời gọi run chưa return.

### Bước 12 — Subscriber cấp c và cài signal handling

1. **Thứ tự / process:** 12; Process subscriber h2.
2. **Hàm/điểm thực thi:** `quic_client_run(options,callbacks,user=&app_s)`.
3. **File/dòng:** [clients/quic_client.c:542-567](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:542).
4. **Đầu vào và nguồn:** Options và callbacks là địa chỉ các biến stack ở caller; user là `&app_s`. `calloc()` cấp một `quic_client`, gồm ring buffer 1 MiB, và khởi tạo các trường về 0.
5. **Lệnh/khối quan trọng:** Kiểm tra options; lưu các con trỏ mượn; đặt `fd = -1`, `stream_id = -1`, `started = now_ns`; khởi tạo GnuTLS global; cài SIGINT/SIGTERM và lưu handler cũ.
6. **Dữ liệu/trạng thái sau bước:** `c_s.user = &app_s`. fd/conn/tls chỉ trở nên hợp lệ khi từng bước khởi tạo tương ứng thành công.
7. **Hàm/sự kiện tiếp:** `socket_open(c_s)`.
8. **Lý do chuyển tiếp:** Biểu thức init short-circuit bắt buộc tạo socket/địa chỉ trước QUIC connection/path, rồi mới nối TLS.

### Bước 13 — Subscriber mở connected UDP socket

1. **Thứ tự / process:** 13; Process subscriber h2 / Linux UDP.
2. **Hàm/điểm thực thi:** `socket_open(c_s)`.
3. **File/dòng:** [clients/quic_client.c:212-237](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:212).
4. **Đầu vào và nguồn:** Host `.254` và chuỗi port `14567` đến từ options; c.remote là sockaddr_in. Socket dùng AF_INET/SOCK_DGRAM/protocol 0; fcntl giữ flags cũ và thêm O_NONBLOCK.
5. **Lệnh/khối quan trọng:** `inet_pton()` và `htons()` dựng remote; `socket()` trả fd; UDP `connect()` đặt peer; `getsockname()` lấy địa chỉ local và cổng nguồn ephemeral.
6. **Dữ liệu/trạng thái sau bước:** c_s.fd là socket trong môi trường mạng h2. Local dự kiến là `10.78.0.12:P_s` theo route [THEORY]; remote là `10.78.0.254:14567`. Source không gọi bind/listen/accept.
7. **Hàm/sự kiện tiếp:** `connection_init(c_s)`.
8. **Lý do chuyển tiếp:** Local/remote đã cụ thể để dựng ngtcp2_path. UDP connect không gửi TCP SYN và không tự chứng minh broker đã sẵn sàng.

### Bước 14 — Subscriber tạo ngtcp2 connection

1. **Thứ tự / process:** 14; Process subscriber h2 / ngtcp2 dependency.
2. **Hàm/điểm thực thi:** `connection_init(c_s)`.
3. **File/dòng:** [clients/quic_client.c:239-282](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:239); [../demo/src/.deps/include/ngtcp2/ngtcp2.h:4173-4201](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/demo/src/.deps/include/ngtcp2/ngtcp2.h:4173).
4. **Đầu vào và nguồn:** Bảng ngtcp2 có 17 callback được gắn. Settings được khởi tạo mặc định rồi đặt initial_ts, handshake timeout 15 giây, UDP payload tối đa 1200 và no_pmtud = 1. Local params cấp cửa sổ nhận 1 MiB, số stream peer mở bằng 0, idle timeout 60 giây, CID limit 2; dcid/scid ngẫu nhiên 16 byte; path lấy local/remote; user_data là c_s.
5. **Lệnh/khối quan trọng:** Gọi `ngtcp2_conn_client_new(&c_s.conn, ..., NGTCP2_PROTO_VER_V1, ..., NULL, c_s)`. Dependency sao chép callback/config theo contract; NULL chọn allocator mặc định.
6. **Dữ liệu/trạng thái sau bước:** c_s.conn đã được cấp phát nhưng `handshake` và `ready` của wrapper vẫn bằng 0. CID là danh tính QUIC, khác PID, MQTT client ID và stream ID.
7. **Hàm/sự kiện tiếp:** `tls_init(c_s)`.
8. **Lý do chuyển tiếp:** Phải nối TLS native handle và crypto bridge trước khi event loop tiến hành handshake.

### Bước 15 — Subscriber nối GnuTLS với ngtcp2

1. **Thứ tự / process:** 15; Process subscriber h2 / GnuTLS + crypto adapter.
2. **Hàm/điểm thực thi:** `tls_init(c_s)`.
3. **File/dòng:** [clients/quic_client.c:284-320](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:284).
4. **Đầu vào và nguồn:** CA path từ options; TLS_PRIORITY chỉ cho TLS 1.3. SNI và tên xác minh là broker.test; ALPN datum chứa mqtt dài 4 byte. conn_ref giữ get_conn và user_data = c_s.
5. **Lệnh/khối quan trọng:** Cấp credentials, nạp CA, tạo GnuTLS session client và cấu hình crypto adapter cho QUIC; đặt priority/credentials/SNI/verify/ALPN. Session giữ `&c_s.conn_ref`, còn ngtcp2 giữ TLS native handle c_s.tls.
6. **Dữ liệu/trạng thái sau bước:** TLS session và credentials được c_s quản lý tới cleanup. Chưa có MQTT CONNECT; app chưa gửi application data trước verification và stream-ready.
7. **Hàm/sự kiện tiếp:** `event_loop(c_s)`.
8. **Lý do chuyển tiếp:** Chỉ sau khi init thành công, run mới in QUIC_CONNECTING và vào event loop.

### Bước 16 — Vòng lặp sinh Initial khi chưa có stream

1. **Thứ tự / process:** 16; Process subscriber h2 / ngtcp2.
2. **Hàm/điểm thực thi:** `event_loop → flush_packets`.
3. **File/dòng:** [clients/quic_client.c:482-511](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:482); [clients/quic_client.c:350-393](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:350).
4. **Đầu vào và nguồn:** `handshake = 0`, `ready = 0`, Q = S = A = 0. Writer chọn id = -1, vectors = NULL/count = 0, packet[1500] trên stack và timestamp ns; output path storage/info được khởi tạo.
5. **Lệnh/khối quan trọng:** `open_stream()` chưa làm gì vì handshake chưa xong. Tick app chưa gọi role tick vì MQTT chưa connected. `ngtcp2_conn_writev_stream()` vẫn có thể tạo packet handshake khi chưa có byte MQTT; crypto client_initial khởi động TLS.
6. **Dữ liệu/trạng thái sau bước:** Nếu writer trả độ dài packet dương, packet được chuyển tới send_datagram. tx_sent không tăng khi ngtcp2 chưa consume byte stream mới.
7. **Hàm/sự kiện tiếp:** `send_datagram()` gọi send UDP; event loop sau đó poll/read.
8. **Lý do chuyển tiếp:** Writer không chỉ phục vụ PUBLISH: stream_id = -1 cho phép thư viện phát handshake và các frame điều khiển QUIC.

### Bước 17 — Initial đi qua mạng mô phỏng tới broker

1. **Thứ tự / process:** 17; Linux/OVS/Docker/EMQX — ngoài call stack C.
2. **Hàm/điểm thực thi:** `send_datagram → send; [THEORY] kernel/OVS/Docker forwarding`.
3. **File/dòng:** [clients/quic_client.c:322-336](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:322); [mininet/topology.py:262-280](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:262); [docker/compose.yaml:11-19](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/docker/compose.yaml:11).
4. **Đầu vào và nguồn:** fd của h2, buffer QUIC packet và số byte writer trả; send dùng flags = 0. Đích L3 là `.254:14567/UDP`, nguồn dự kiến `.12:P_s` theo socket.
5. **Lệnh/khối quan trọng:** `send()` nộp datagram cho kernel. Đường cấu hình là h2-eth0 → veth s1-eth2 → s1 → mn-gw; Docker published-port đưa tới container theo backend/rules thực tế chưa được đo trong lượt này.
6. **Dữ liệu/trạng thái sau bước:** Send trả đủ byte không tự thay đổi trạng thái MQTT hoặc chứng minh peer đã nhận. Source project không cho biết source NAT, IP container hay tên veth Docker cụ thể lúc chạy.
7. **Hàm/sự kiện tiếp:** Broker xử lý handshake và phản hồi về socket h2.
8. **Lý do chuyển tiếp:** EMQX được cấu hình listener QUIC 14567. Các hàm nhận/crypto nội bộ broker thuộc binary bên ngoài nên không đặt tên hàm giả.

### Bước 18 — Subscriber nhận UDP và đưa packet vào connection

1. **Thứ tự / process:** 18; Process subscriber h2.
2. **Hàm/điểm thực thi:** `event_loop poll → read_packets → recv → ngtcp2_conn_read_pkt`.
3. **File/dòng:** [clients/quic_client.c:452-479](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:452); [clients/quic_client.c:518-538](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:518).
4. **Đầu vào và nguồn:** pollfd dùng c_s.fd với events = POLLIN; packet[65536] nằm trên stack; recv flags = 0 trả độ dài thực tế. Path lấy từ path_for(c_s), packet info bằng 0, connection là c_s.conn và timestamp được lấy mới.
5. **Lệnh/khối quan trọng:** POLLIN khiến loop gọi read_packets. recv gặp EAGAIN/EWOULDBLOCK/EINTR thì trả 0 về loop; khi có byte, truyền đúng số byte nhận vào ngtcp2. Chưa parse MQTT trực tiếp trên UDP buffer.
6. **Dữ liệu/trạng thái sau bước:** Dependency kiểm tra packet/CID/packet number/crypto và cập nhật state của connection do caller chọn. Client project không có bảng server-style CID map để chọn giữa nhiều connection.
7. **Hàm/sự kiện tiếp:** Các crypto callback/handshake_completed được gọi nếu đủ điều kiện, hoặc chương trình tiếp tục chờ.
8. **Lý do chuyển tiếp:** Connected socket giới hạn peer của traffic nhận và mỗi process có c.conn riêng. Handshake có thể cần nhiều vòng đọc/ghi; không cố định số datagram.

### Bước 19 — Subscriber xác minh handshake và mở stream

1. **Thứ tự / process:** 19; Process subscriber h2.
2. **Hàm/điểm thực thi:** `handshake_completed → ngtcp2_conn_read_pkt return → read_packets gọi open_stream`.
3. **File/dòng:** [clients/quic_client.c:122-143](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:122); [clients/quic_client.c:338-347](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:338); [clients/quic_client.c:475-478](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:475).
4. **Đầu vào và nguồn:** Callback nhận user = c_s từ constructor. TLS session phải đạt TLS 1.3, ALPN mqtt và certificate verification với broker.test; open_stream nhận c_s.conn và địa chỉ output stream_id.
5. **Lệnh/khối quan trọng:** Chỉ sau verification thành công mới đặt handshake = 1. Nếu ngtcp2_conn_open_bidi_stream trả STREAM_ID_BLOCKED thì chờ thử tiếp; nếu mở thành công, đặt ready = 1 rồi gọi callback ready(c_s, c_s.user).
6. **Dữ liệu/trạng thái sau bước:** c_s.stream_id được tạo; ID đầu tiên là 0 trong luồng này. Handshake và ready là hai cờ riêng, còn c_s.user vẫn giữ &app_s.
7. **Hàm/sự kiện tiếp:** `on_ready(client = c_s, user = &app_s)`.
8. **Lý do chuyển tiếp:** Có UDP fd chưa đủ để gọi ready; phải có handshake được xác minh và quyền mở stream từ peer.

### Bước 20 — Subscriber queue MQTT CONNECT

1. **Thứ tự / process:** 20; Process subscriber h2.
2. **Hàm/điểm thực thi:** `on_ready → mqtt_encode_connect → mqtt_app_send → quic_client_send`.
3. **File/dòng:** [clients/app.c:37-47](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:37); [clients/mqtt_codec.c:38-43](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:38); [clients/app.c:13-21](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:13); [clients/quic_client.c:63-76](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:63).
4. **Đầu vào và nguồn:** client là c_s, user là &app_s; client ID là chuỗi mặc định, keepalive bằng 20. Buffer ban đầu zero; kết quả encoder là rc; với ID ngắn trong kịch bản, CONNECT dài 14 + n_s byte.
5. **Lệnh/khối quan trọng:** Đặt app_s.quic = c_s; encoder cấp buffer, ghi các số bằng put16 và copy bytes. mqtt_app_send kiểm lỗi, transport copy vào ring; buffer encode được free và last_tx cập nhật theo ms.
6. **Dữ liệu/trạng thái sau bước:** Q_s = 14 + n_s; S_s/A_s có thể vẫn bằng 0 ngay khi queue. connect_sent = 1, deadline = now_ms + 15000, nhưng app_s.connected vẫn bằng 0.
7. **Hàm/sự kiện tiếp:** Callback return qua open_stream/read_packets; event loop tiếp tục flush CONNECT.
8. **Lý do chuyển tiếp:** Queue chỉ chuẩn bị byte. Encode hoặc gán connect_sent không tự gửi; network writer chạy sau khi quay về event loop ngoài callback.

### Bước 21 — CONNECT được consume và gửi; broker trả CONNACK

1. **Thứ tự / process:** 21; Subscriber transport ↔ EMQX ngoài source.
2. **Hàm/điểm thực thi:** `flush_packets → ngtcp2_conn_writev_stream → send_datagram; broker boundary`.
3. **File/dòng:** [clients/quic_client.c:350-393](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:350); [clients/app.c:49-61](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:49).
4. **Đầu vào và nguồn:** Vector trỏ vào ring tại tx_sent modulo capacity; count bằng 1 hoặc 2 khi wrap; stream ID là ID vừa mở, flags bằng 0. Output consumed và độ dài packet là hai giá trị khác nhau.
5. **Lệnh/khối quan trọng:** Khi có packet dương, S_s tăng theo consumed hợp lệ rồi packet được gửi UDP. Broker xử lý QUIC và CONNECT bằng implementation bên ngoài project, sau đó trả CONNACK trên connection S.
6. **Dữ liệu/trạng thái sau bước:** QUIC ACK có thể cập nhật A_s độc lập. app_s.connected chỉ đổi khi parser và handler chấp nhận CONNACK.
7. **Hàm/sự kiện tiếp:** `read_packets()` của subscriber xử lý packet có STREAM bytes.
8. **Lý do chuyển tiếp:** Không thể truy vết tên hàm broker từ source hiện có; decoder/app ở bước tiếp theo là nơi project kiểm CONNACK nhận được.

### Bước 22 — ngtcp2 gọi receive_stream rồi on_data

1. **Thứ tự / process:** 22; Process subscriber h2.
2. **Hàm/điểm thực thi:** `ngtcp2_conn_read_pkt → receive_stream → own data callback`.
3. **File/dòng:** [clients/quic_client.c:145-172](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:145); [clients/app.c:66-74](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:66).
4. **Đầu vào và nguồn:** ngtcp2 cấp conn = c_s.conn, flags/id/offset, stream plaintext data/length, user = c_s và stream_user = NULL. Callback app nhận client = c_s, cùng data/length, nhưng user = c_s.user = &app_s.
5. **Lệnh/khối quan trọng:** Kiểm ready, đúng stream, offset = rx_received, chưa nhận FIN trước đó và không phải 0-RTT. Chỉ length > 0 mới gọi on_data; on_data chuyển user về mqtt_app* rồi feed parser.
6. **Dữ liệu/trạng thái sau bước:** Đoạn byte có thể chỉ là một phần CONNACK hoặc chứa nhiều MQTT packet. Không dùng strlen hoặc %s cho binary bytes.
7. **Hàm/sự kiện tiếp:** `mqtt_parser_feed(&app_s.parser, data, length, on_packet, app_s)`.
8. **Lý do chuyển tiếp:** Chỉ stream application bytes được chuyển tới app; packet chỉ mang QUIC ACK/CRYPTO không tự kích hoạt on_data.

### Bước 23 — Parser tách CONNACK và gọi session handler

1. **Thứ tự / process:** 23; Process subscriber h2.
2. **Hàm/điểm thực thi:** `mqtt_parser_feed → header → on_packet`.
3. **File/dòng:** [clients/mqtt_codec.c:59-102](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:59); [clients/app.c:49-64](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:49).
4. **Đầu vào và nguồn:** p là &app_s.parser, cb là on_packet, arg là &app_s. CONNACK được chấp nhận có bytes `20 02 00 00`; header cho nh = 2, nb = 2; packet view có type = 2, flags = 0, body = `00 00`, len = 2.
5. **Lệnh/khối quan trọng:** Parser tích lũy qua nhiều lần feed nếu cần; đủ 4 byte mới gọi cb(&packet, arg). on_packet thấy connected = 0 nên kiểm connect_sent và decode CONNACK/session_present/return_code.
6. **Dữ liệu/trạng thái sau bước:** CONNACK hợp lệ khiến app_s.connected = 1. Packet/body view chỉ dùng trong callback hiện tại; parser có thể tái sử dụng buffer sau return.
7. **Hàm/sự kiện tiếp:** `app_s.role.connected(app_s)` gọi subscriber_connected.
8. **Lý do chuyển tiếp:** Đủ header chưa có nghĩa đủ packet. Role connected chỉ chạy sau khi có packet đầy đủ và tất cả kiểm tra ngữ nghĩa đã thành công.

### Bước 24 — Subscriber queue SUBSCRIBE và đặt deadline

1. **Thứ tự / process:** 24; Process subscriber h2.
2. **Hàm/điểm thực thi:** `subscriber_connected → mqtt_encode_subscribe → mqtt_app_send`.
3. **File/dòng:** [clients/subscriber.c:20-32](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/subscriber.c:20); [clients/mqtt_codec.c:44-48](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:44).
4. **Đầu vào và nguồn:** app_s.role_data trỏ tới context subscriber trong main; packet ID bằng 1, topic dài 15 byte, QoS yêu cầu bằng 0. Encoder allocate header 0x82, ghi ID/độ dài topic bằng put16 và ghi byte QoS bằng 0.
5. **Lệnh/khối quan trọng:** Queue 22 byte vào c_s.tx, free buffer encode; đặt state = WAIT_SUBACK, subscribe_deadline = now_ms + 15000 rồi in SUBSCRIBE sent.
6. **Dữ liệu/trạng thái sau bước:** Q_s tăng 22 và last_tx cập nhật. WAIT_SUBACK lúc này biểu thị yêu cầu đã queue và đang chờ phản hồi, thay vì chỉ là giá trị initializer.
7. **Hàm/sự kiện tiếp:** Return qua on_packet → feed → on_data → receive_stream → ngtcp2/read_packets; sau đó event loop flush SUBSCRIBE.
8. **Lý do chuyển tiếp:** Cùng một luồng thực thi nên role handler đặt deadline xong trước lần role tick tiếp theo; không có tick thread riêng chạy tranh chấp.

### Bước 25 — Sau callback nhận, cập nhật flow control

1. **Thứ tự / process:** 25; Process subscriber h2.
2. **Hàm/điểm thực thi:** `receive_stream sau on_data success`.
3. **File/dòng:** [clients/quic_client.c:156-171](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:156).
4. **Đầu vào và nguồn:** length là số stream byte ngtcp2 cung cấp trong lần callback này; id là stream ứng dụng. Callback app đã trả 0.
5. **Lệnh/khối quan trọng:** Tăng rx_received theo length; gọi ngtcp2_conn_extend_max_stream_offset(conn, id, length) và ngtcp2_conn_extend_max_offset(conn, length). Nếu có FIN, đặt rx_fin và kiểm tra c đang finishing.
6. **Dữ liệu/trạng thái sau bước:** Credit cửa sổ nhận được mở thêm theo số byte chấp nhận, không theo số MQTT packet. Nếu app callback lỗi thì không thực hiện các cập nhật tiếp theo này.
7. **Hàm/sự kiện tiếp:** ngtcp2_conn_read_pkt return; read_packets gọi open_stream (không làm gì nếu đã ready), rồi đọc tiếp batch hoặc quay về loop.
8. **Lý do chuyển tiếp:** Flow-control extension là điều khiển QUIC, không phải MQTT SUBACK hoặc bằng chứng message đã được xử lý tại process khác.

### Bước 26 — SUBACK đưa subscriber sang sẵn sàng nhận

1. **Thứ tự / process:** 26; Process subscriber h2 ↔ EMQX boundary.
2. **Hàm/điểm thực thi:** `flush SUBSCRIBE; nhận SUBACK → on_packet → subscriber_packet`.
3. **File/dòng:** [clients/subscriber.c:34-48](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/subscriber.c:34); [clients/mqtt_codec.c:103-106](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:103); [clients/app.c:63](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:63).
4. **Đầu vào và nguồn:** Broker trả SUBACK. Decoder kiểm type = 9, flags = 0, body len = 3; role yêu cầu packet ID = 1 và granted QoS = 0. Bytes được chấp nhận là `90 03 00 01 00`.
5. **Lệnh/khối quan trọng:** App đã connected nên gọi role.packet; type 9 và state WAIT_SUBACK chọn đúng nhánh. Chỉ sau validation thành công mới chuyển SUBSCRIBED và in marker.
6. **Dữ liệu/trạng thái sau bước:** Subscriber sẵn sàng xử lý PUBLISH type 3 trên đúng topic; received chưa tăng. Nếu im lặng đủ lâu, tick có thể gửi ping.
7. **Hàm/sự kiện tiếp:** Python wait_subscribed nhìn thấy SUBACK accepted trong log.
8. **Lý do chuyển tiếp:** Marker được in sau khi chuyển state thành công. stdout line buffering giúp dòng readiness được ghi ra file để orchestration kiểm tra.

### Bước 27 — Orchestrator mở gate và launch publisher

1. **Thứ tự / process:** 27; Python topology.py → process publisher h1.
2. **Hàm/điểm thực thi:** `wait_subscribed return → h1.popen`.
3. **File/dòng:** [mininet/topology.py:156-165](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:156); [mininet/topology.py:184-192](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:184).
4. **Đầu vào và nguồn:** wait_subscribed nhận Popen subscriber, log_path và timeout Python mặc định 25 giây. Nó kiểm marker trước poll process. Lệnh publisher dùng cùng endpoint/CA/topic và message mẫu.
5. **Lệnh/khối quan trọng:** Launch process trên h1 với cwd = ROOT và log riêng; sau đó Python wait publisher rồi wait subscriber với timeout riêng cho từng giai đoạn chờ.
6. **Dữ liệu/trạng thái sau bước:** Từ đây hai C event loop hoạt động độc lập; Python không trực tiếp gọi các hàm C pub/sub.
7. **Hàm/sự kiện tiếp:** publisher.main → mqtt_app_main → quic_client_run.
8. **Lý do chuyển tiếp:** Gate SUBACK bảo đảm demo không phát QoS 0 trước khi đăng ký xong; nó không chứng minh broker sẽ lưu lại message trong trường hợp gửi sớm.

### Bước 28 — Publisher tạo context và connection riêng

1. **Thứ tự / process:** 28; Process publisher h1.
2. **Hàm/điểm thực thi:** `publisher.main → mqtt_app_main → quic_client_run → socket/connection/TLS/event_loop`.
3. **File/dòng:** [clients/publisher.c:42-49](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/publisher.c:42); [clients/app.c:115-161](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:115); [clients/quic_client.c:542-570](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:542).
4. **Đầu vào và nguồn:** Biến pub trên stack có publish_deadline = 0; role publisher có default_timeout = 30. app_p.message lấy từ argv, role_data = &pub; bảng callback giống nhau về hàm nhưng user = &app_p.
5. **Lệnh/khối quan trọng:** Chạy chuỗi socket/QUIC/TLS/handshake như subscriber, nhưng c_p, fd_p, CID, cổng P_p và ring đều thuộc process riêng. Endpoint vẫn là `.254:14567`; local dự kiến `10.78.0.11:P_p`.
6. **Dữ liệu/trạng thái sau bước:** c_p.handshake, c_p.ready và app_p.connected tiến triển riêng; không dùng chung app_s hoặc state đăng ký của h2.
7. **Hàm/sự kiện tiếp:** on_ready của publisher queue CONNECT; sau đó nhận CONNACK hợp lệ.
8. **Lý do chuyển tiếp:** Broker quản lý hai session/connection. Không chuyển socket hoặc object c_s từ h2 sang h1.

### Bước 29 — CONNACK publisher kích hoạt tạo payload

1. **Thứ tự / process:** 29; Process publisher h1.
2. **Hàm/điểm thực thi:** `on_packet → publisher_connected`.
3. **File/dòng:** [clients/app.c:49-64](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:49); [clients/publisher.c:11-20](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/publisher.c:11).
4. **Đầu vào và nguồn:** on_packet nhận CONNACK trên connection P với user = &app_p. role.connected trỏ publisher_connected; topic là lab/temperature, message là Temperature = 28.5 C.
5. **Lệnh/khối quan trọng:** Đặt app_p.connected = 1 và gọi role.connected. Publisher lấy pub từ role_data rồi gọi mqtt_encode_publish với strlen(message) = 20.
6. **Dữ liệu/trạng thái sau bước:** Payload vẫn là chuỗi argv dài 20 byte, chưa phải UDP packet. MQTT client ID dùng trong CONNECT, không được chèn vào PUBLISH.
7. **Hàm/sự kiện tiếp:** mqtt_encode_publish → valid_topic/allocate/put16/memcpy.
8. **Lý do chuyển tiếp:** Role quyết định hành vi sau CONNACK: cùng code session, nhưng subscriber queue SUBSCRIBE còn publisher queue PUBLISH.

### Bước 30 — Encode PUBLISH đầy đủ 39 byte

1. **Thứ tự / process:** 30; Process publisher h1.
2. **Hàm/điểm thực thi:** `mqtt_encode_publish(buffer,topic,payload,n=20)`.
3. **File/dòng:** [clients/mqtt_codec.c:5-6](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:5); [clients/mqtt_codec.c:8-36](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:8); [clients/mqtt_codec.c:49-54](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:49).
4. **Đầu vào và nguồn:** buffer là biến stack khởi tạo zero; topic trỏ argv/default và dài nt = 15; payload là const byte pointer tới message, n = 20. allocate nhận header 0x30, remaining = 37 và trả body view trong buffer heap.
5. **Lệnh/khối quan trọng:** valid_topic kiểm tra topic không rỗng, UTF-8 hợp lệ và không chứa +/#. allocate tạo fixed header với Remaining Length 0x25; put16 ghi `00 0F`; memcpy nối topic và payload.
6. **Dữ liệu/trạng thái sau bước:** buffer.len = 39; buffer.data sở hữu 39 byte MQTT đã encode. Chưa có QUIC framing, UDP header hoặc packet number ở bước này.
7. **Hàm/sự kiện tiếp:** `mqtt_app_send(app_p, &buffer, rc)`.
8. **Lý do chuyển tiếp:** Encode thành công trả 0. Nếu encoder lỗi, mqtt_app_send short-circuit, không gọi quic_client_send, vẫn free buffer rồi trả lỗi.

### Bước 31 — Copy vào retransmission queue; giải phóng encode buffer

1. **Thứ tự / process:** 31; Process publisher h1.
2. **Hàm/điểm thực thi:** `mqtt_app_send → quic_client_send`.
3. **File/dòng:** [clients/app.c:13-21](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:13); [clients/quic_client.c:59-76](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:59); [clients/publisher.c:14-18](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/publisher.c:14).
4. **Đầu vào và nguồn:** app_p.quic là c_p; data = buffer.data, length = 39; pending = Q_p - A_p. c đã ready, chưa finishing; ring capacity là 1 MiB.
5. **Lệnh/khối quan trọng:** Kiểm tra chỗ trống và counter overflow; lấy pos = Q modulo capacity; copy đoạn đầu và đoạn wrap nếu cần; tăng Q thêm 39. mqtt_app_send free buffer và cập nhật last_tx; role đặt publish_deadline = now_ms + 15000.
6. **Dữ liệu/trạng thái sau bước:** Byte gửi không còn phụ thuộc buffer encode đã free. Q_p tăng 39; S_p và A_p chưa bắt buộc tăng. Log PUBLISH sent đánh dấu queue thành công.
7. **Hàm/sự kiện tiếp:** Return qua role/session/parser/transport callback; event loop tiếp tục flush.
8. **Lý do chuyển tiếp:** Callback nhận không gọi trực tiếp ngtcp2 writer. Queue giữ ổn định vùng byte mà thư viện có thể cần truyền lại tới khi ACK.

### Bước 32 — ngtcp2 consume stream bytes và sinh packet QUIC

1. **Thứ tự / process:** 32; Process publisher h1.
2. **Hàm/điểm thực thi:** `event_loop → flush_packets → ngtcp2_conn_writev_stream`.
3. **File/dòng:** [clients/quic_client.c:350-393](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:350); [../demo/src/.deps/include/ngtcp2/ngtcp2.h:5201-5357](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/demo/src/.deps/include/ngtcp2/ngtcp2.h:5201).
4. **Đầu vào và nguồn:** conn = c_p.conn; packet[1500] là output stack; consumed ban đầu -1; stream ID là c_p.stream_id; vectors trỏ ring tại S modulo capacity, count = 1 hoặc 2; flags = 0; timestamp lấy theo ns.
5. **Lệnh/khối quan trọng:** Writer có thể packetize một phần dữ liệu hoặc tạo packet chỉ mang ACK/crypto. STREAM_DATA_BLOCKED khiến wrapper thử lại với id = -1 để vẫn gửi control. Khi packet length dương, tăng S theo consumed hợp lệ rồi gọi send_datagram.
6. **Dữ liệu/trạng thái sau bước:** Không giả định PUBLISH 39 byte nằm trọn trong một datagram. Packet number, QUIC frame, AEAD và header protection thuộc ngtcp2/crypto adapter.
7. **Hàm/sự kiện tiếp:** `send_datagram(c_p, packet, length)` khi có output dương.
8. **Lý do chuyển tiếp:** Chỉ length > 0 mới nộp datagram; length = 0 kết thúc batch flush để loop tiếp tục đọc, xử lý timer hoặc chờ tiến triển.

### Bước 33 — UDP send từ h1 và đường tới host endpoint

1. **Thứ tự / process:** 33; Process publisher h1 → Linux/OVS.
2. **Hàm/điểm thực thi:** `send_datagram → send(fd_p,data,length,0)`.
3. **File/dòng:** [clients/quic_client.c:322-336](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:322); [mininet/topology.py:262-276](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:262).
4. **Đầu vào và nguồn:** fd_p là UDP socket đã connect. data mượn packet buffer trên stack của flush; length do ngtcp2 trả. send_datagram tạo deadline now_ns + 1 giây cho nhánh would-block.
5. **Lệnh/khối quan trọng:** Send trả đủ length thì helper trả 0; EINTR thử lại; EAGAIN/EWOULDBLOCK còn trước deadline thì poll POLLOUT từng 10 ms rồi thử tiếp. Nhánh EINTR không kiểm deadline nên 1 giây không phải hard bound tổng. [THEORY] kernel dùng route on-link `.78/24`, resolve MAC đích `.254` bằng ARP khi cần, rồi qua h1-eth0 → s1-eth1 → s1 → mn-gw.
6. **Dữ liệu/trạng thái sau bước:** Kernel chấp nhận QUIC bytes chưa có nghĩa broker đã nhận. C source không gọi syscall riêng cho từng interface; packet path nằm ngoài call stack ứng dụng.
7. **Hàm/sự kiện tiếp:** Docker published-port path đưa datagram tới container.
8. **Lý do chuyển tiếp:** `.254` là địa chỉ host trong cùng subnet; client không cần default gateway Internet để tới đó. Đường Docker bắt đầu sau khi packet đi vào host.

### Bước 34 — Docker/container và broker nhận publication

1. **Thứ tự / process:** 34; Host kernel/Docker/container EMQX — boundary ngoài source.
2. **Hàm/điểm thực thi:** `Published UDP14567 → listener QUIC → xử lý MQTT nội bộ chưa truy được hàm`.
3. **File/dòng:** [docker/compose.yaml:11-25](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/docker/compose.yaml:11); [docker/emqx.conf:17-31](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/docker/emqx.conf:17); [work/mininet-separated-clients/subscriber.log:8](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/work/mininet-separated-clients/subscriber.log:8).
4. **Đầu vào và nguồn:** Đích host `.254:14567` được Docker cấu hình publish tới port 14567 của container. Source address/NAT tùy backend và rules thực tế; certificate và listener được cấu hình ở broker.
5. **Lệnh/khối quan trọng:** [CONFIG/THEORY] Datagram được đưa tới broker. Broker kết thúc QUIC connection P, đọc byte MQTT PUBLISH và phân phối theo subscription/topic. Không đặt tên recv/dispatch EMQX giả vì không có source image để truy vết nội bộ.
6. **Dữ liệu/trạng thái sau bước:** ACK trên connection P và PUBLISH trên connection S là hai sự kiện khác nhau. Log h2 cung cấp bằng chứng chức năng; nó chưa chứng minh từng firewall hop.
7. **Hàm/sự kiện tiếp:** Hai nhánh độc lập: transport ACK cho publisher và broker chuyển message tới subscriber.
8. **Lý do chuyển tiếp:** Hai connection có khóa, CID và packet number riêng. Broker không chuyển nguyên ciphertext từ publisher sang h2.

### Bước 35 — Nhánh A: QUIC ACK giải phóng byte queue publisher

1. **Thứ tự / process:** 35; Process publisher h1 / ngtcp2.
2. **Hàm/điểm thực thi:** `read_packets → ngtcp2 → acked_stream`.
3. **File/dòng:** [clients/quic_client.c:174-188](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:174); [../demo/src/.deps/include/ngtcp2/ngtcp2.h:3028-3050](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/demo/src/.deps/include/ngtcp2/ngtcp2.h:3028).
4. **Đầu vào và nguồn:** Callback nhận id/offset/length là stream range do ngtcp2 báo; user = c_p, stream_user = NULL. Offset phải bằng A_p, length không vượt S_p - A_p.
5. **Lệnh/khối quan trọng:** Kiểm tra rồi cộng A_p += length. Không đếm một ACK packet thành một byte và không xử lý MQTT PUBACK. Vùng byte đã ACK mới được queue tái sử dụng khi ring quay vòng.
6. **Dữ liệu/trạng thái sau bước:** pending = Q_p - A_p giảm; vẫn có thể khác 0 nếu chưa ACK hết. Khi về 0, publisher_tick có thể yêu cầu disconnect nếu chưa hết deadline.
7. **Hàm/sự kiện tiếp:** publisher_tick trong lần kiểm tra tiếp theo của event loop.
8. **Lý do chuyển tiếp:** ACK callback do state ngtcp2 kích hoạt; app không tự parse QUIC ACK frame từ UDP buffer.

### Bước 36 — Nhánh A: publisher yêu cầu đóng sau ACK

1. **Thứ tự / process:** 36; Process publisher h1.
2. **Hàm/điểm thực thi:** `on_tick → publisher_tick → mqtt_app_disconnect`.
3. **File/dòng:** [clients/app.c:76-88](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:76); [clients/publisher.c:29-39](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/publisher.c:29); [clients/app.c:24-34](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:24).
4. **Đầu vào và nguồn:** now_ms do event loop cung cấp; pub.publish_deadline được đặt sau queue PUBLISH; app_p đã connected và chưa stopping; helper đọc pending của c_p.
5. **Lệnh/khối quan trọng:** Publisher kiểm deadline trước: nếu đã hết hạn thì lỗi ngay, kể cả pending vừa về 0. Nếu chưa hết hạn và pending = 0, in marker ACK rồi gọi mqtt_app_disconnect để queue DISCONNECT, đặt stopping và yêu cầu finish.
6. **Dữ liệu/trạng thái sau bước:** Q_p tăng 2 cho DISCONNECT; c_p.finishing = 1, finish_deadline = now_ns + 15 giây. Hai byte DISCONNECT cũng phải được QUIC ACK trước khi gửi FIN.
7. **Hàm/sự kiện tiếp:** Nhánh drain/FIN bên dưới. Nhánh B có thể xảy ra trước, trong hoặc sau tiến trình của nhánh A.
8. **Lý do chuyển tiếp:** PUBLISH được ACK bởi broker chưa chứng minh h2 đã nhận. Chỉ được kết luận giao hàng ứng dụng end-to-end sau khi theo tới đầu nhận ở nhánh B.

### Bước 37 — Nhánh B: broker phát PUBLISH trên connection subscriber

1. **Thứ tự / process:** 37; EMQX boundary → host/Docker/OVS → h2.
2. **Hàm/điểm thực thi:** `[THEORY/CONFIG] mạng → recv socket c_s.fd`.
3. **File/dòng:** [docker/compose.yaml:11-19](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/docker/compose.yaml:11); [mininet/topology.py:262-280](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:262); [clients/quic_client.c:452-464](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:452).
4. **Đầu vào và nguồn:** Connection S gắn với endpoint subscriber `.12:P_s`; broker dùng session/subscription đã chấp nhận. Đường trả qua conntrack/NAT hoặc proxy cần kiểm tra thực tế, không gán cứng IP 172.x.
5. **Lệnh/khối quan trọng:** Packet trở về mn-gw → s1 → s1-eth2 → h2-eth0 theo datapath phù hợp, rồi UDP demultiplex tới socket h2. Đây là QUIC packet của connection S, không phải connection P.
6. **Dữ liệu/trạng thái sau bước:** Socket subscriber có datagram mã hóa; message chưa được in stdout. MAC, veth Docker và IP container runtime chưa được đo trong lượt phân tích.
7. **Hàm/sự kiện tiếp:** `read_packets(c_s)` → `ngtcp2_conn_read_pkt(c_s.conn, ...)`.
8. **Lý do chuyển tiếp:** POLLIN khiến wrapper đọc; message xuất hiện ở app sau khi QUIC xử lý bảo vệ packet và parser MQTT ghép đủ packet.

### Bước 38 — Nhánh B: stream bytes tới parser subscriber

1. **Thứ tự / process:** 38; Process subscriber h2.
2. **Hàm/điểm thực thi:** `ngtcp2 → receive_stream → on_data → mqtt_parser_feed`.
3. **File/dòng:** [clients/quic_client.c:145-172](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:145); [clients/app.c:66-74](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:66); [clients/mqtt_codec.c:78-98](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:78).
4. **Đầu vào và nguồn:** conn là c_s.conn; stream ID thuộc connection S; offset phải khớp rx_received của c_s. Byte ứng dụng PUBLISH có thể bị chia nhỏ; user của callback ngtcp2 là c_s, còn user của on_data là &app_s.
5. **Lệnh/khối quan trọng:** Các guard stream/offset/0-RTT phải qua trước khi feed. Parser đọc type = 3, flags = 0 và Remaining Length = 37 trong kịch bản này; chỉ đủ 39 byte mới tạo packet có body len = 37 rồi gọi on_packet.
6. **Dữ liệu/trạng thái sau bước:** Không phụ thuộc ranh giới chunk callback. nh = 2, nb = 37; packet.body trỏ vào parser buffer, không phải UDP buffer hoặc argv của publisher.
7. **Hàm/sự kiện tiếp:** `on_packet(packet, &app_s)` gọi role.packet.
8. **Lý do chuyển tiếp:** app_s.connected đã bằng 1 từ CONNACK trước đó, nên không decode CONNACK hoặc gọi subscriber_connected lần nữa.

### Bước 39 — Nhánh B: decode topic và payload

1. **Thứ tự / process:** 39; Process subscriber h2.
2. **Hàm/điểm thực thi:** `subscriber_packet → mqtt_decode_publish`.
3. **File/dòng:** [clients/subscriber.c:50-58](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/subscriber.c:50); [clients/mqtt_codec.c:107-113](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:107).
4. **Đầu vào và nguồn:** packet.type = 3, state = SUBSCRIBED; decoder nhận địa chỉ biến mqtt_publish message trên stack. Body len bằng 37, hai byte đầu là `00 0F`; get16 cho nt = 15.
5. **Lệnh/khối quan trọng:** Decoder kiểm type/flags được hỗ trợ ở QoS 0, body có ít nhất 2 byte, nt ≤ len - 2 và valid_topic. Gán topic view = body + 2, payload view = body + 2 + nt, payload_len = 20. Subscriber còn so độ dài topic và memcmp với app_s.topic.
6. **Dữ liệu/trạng thái sau bước:** message chứa các view mượn, chưa copy dữ liệu riêng. Topic khác hoặc QoS/format không hỗ trợ làm hàm trả -1 và unwind; không in payload như một lần nhận thành công.
7. **Hàm/sự kiện tiếp:** fputs/fwrite/putchar in topic và payload.
8. **Lý do chuyển tiếp:** Chỉ sau validation mới đọc và hiển thị payload; type 3 tự nó không chứng minh đây là message temperature đúng topic.

### Bước 40 — Nhánh B: dữ liệu đến thành phần đích

1. **Thứ tự / process:** 40; Process subscriber h2 / stdout log.
2. **Hàm/điểm thực thi:** `subscriber_packet: fwrite và ++received`.
3. **File/dòng:** [clients/subscriber.c:60-70](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/subscriber.c:60); [mininet/topology.py:184-185](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:184).
4. **Đầu vào và nguồn:** message.topic_len = 15, payload_len = 20. Popen đã chuyển stdout của process h2 vào subscriber.log; received ban đầu bằng 0, app_s.count bằng 1.
5. **Lệnh/khối quan trọng:** fwrite ghi theo độ dài, không yêu cầu NUL; putchar thêm newline; tăng received. Vì count khác 0 và received ≥ count, gọi mqtt_app_disconnect.
6. **Dữ liệu/trạng thái sau bước:** Log có đúng topic/payload và received = 1. Đây là điểm app đích nhận message trong luồng được phân tích, vượt ra ngoài bằng chứng transport ACK của publisher.
7. **Hàm/sự kiện tiếp:** Queue DISCONNECT của subscriber và yêu cầu drain; return qua parser/ngtcp2.
8. **Lý do chuyển tiếp:** Auto-test truyền count = 1 nên subscriber đóng sau message đầu. Chế độ tương tác mặc định count = 0 sẽ tiếp tục chờ message và duy trì keepalive.

### Bước 41 — Hai phía queue DISCONNECT và giữ dữ liệu đến ACK

1. **Thứ tự / process:** 41; Mỗi process client độc lập.
2. **Hàm/điểm thực thi:** `mqtt_app_disconnect → mqtt_encode_disconnect → mqtt_app_send → quic_client_finish`.
3. **File/dòng:** [clients/app.c:24-34](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:24); [clients/mqtt_codec.c:56](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:56); [clients/quic_client.c:78-84](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:78).
4. **Đầu vào và nguồn:** Mỗi app đã connected, stopping ban đầu bằng 0. Buffer encoder zero; header DISCONNECT là 0xE0, Remaining Length = 0. handshake_timeout_s = 15 được tái sử dụng làm khoảng giới hạn drain.
5. **Lệnh/khối quan trọng:** Queue hai byte `E0 00`; đặt stopping = 1, finishing = 1 và finish_deadline. Nếu đã stopping, helper return 0 và không queue DISCONNECT lặp.
6. **Dữ liệu/trạng thái sau bước:** Q tăng 2. Khi c.finishing đã bật, event loop bỏ role tick nhưng vẫn giữ socket, crypto context và tx buffer để gửi, truyền lại và xử lý ACK.
7. **Hàm/sự kiện tiếp:** Event loop flush DISCONNECT, nhận ACK cho application bytes, rồi mới gửi FIN-only.
8. **Lý do chuyển tiếp:** finish chỉ đặt cờ/yêu cầu kết thúc; không free c hoặc đóng socket ngay trong callback.

### Bước 42 — FIN-only sau khi mọi application byte đã ACK

1. **Thứ tự / process:** 42; Mỗi process client / ngtcp2.
2. **Hàm/điểm thực thi:** `flush_packets → empty FIN → acked_stream`.
3. **File/dòng:** [clients/quic_client.c:350-389](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:350); [clients/quic_client.c:174-188](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:174).
4. **Đầu vào và nguồn:** send_fin được tính bằng finishing && pending == 0. Writer dùng stream ID hợp lệ, vector dài 0 và cờ FIN. Khi packet dương có consumed hợp lệ và đúng stream, tx_fin được đặt; ACK callback length = 0 sau empty FIN có thể đặt fin_acked.
5. **Lệnh/khối quan trọng:** Chờ mọi application byte được ACK trước khi gửi empty FIN để phân biệt ACK dữ liệu với xác nhận FIN. Nếu chưa hoàn tất, loop tiếp tục timer/read/flush hoặc chuyển sang nhánh peer-close được chấp nhận ở bước 43.
6. **Dữ liệu/trạng thái sau bước:** finishing = 1 và tx_fin = 1 sau khi FIN được serialize/gửi theo nhánh thành công. ACK empty FIN đặt fin_acked = 1; pending vẫn bằng 0. rx_fin/stream_closed có thể cập nhật ở callback riêng.
7. **Hàm/sự kiện tiếp:** Event loop xét nhánh FIN-ACK để in QUIC_DRAIN_OK và gọi finish_close; hoặc read_packets nhận peer close và áp dụng điều kiện thành công riêng ở bước 43.
8. **Lý do chuyển tiếp:** Nhánh đóng chủ động không chỉ kiểm Q == S: cần Q == A và FIN được ACK. Nhánh peer-close có tiêu chí khác, không bắt buộc fin_acked.

### Bước 43 — Hai nhánh hoàn tất transport

1. **Thứ tự / process:** 43; Mỗi process client / ngtcp2.
2. **Hàm/điểm thực thi:** `event_loop → finish_close hoặc read_packets return1`.
3. **File/dòng:** [clients/quic_client.c:426-450](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:426); [clients/quic_client.c:465-468](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:465); [clients/quic_client.c:512-536](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:512).
4. **Đầu vào và nguồn:** Nhánh chủ động cần finishing, tx_fin, fin_acked và pending = 0. Nhánh peer-close cần ngtcp2 trả DRAINING, đang finishing, đã gửi FIN, pending = 0 và peer error_code = 0; không yêu cầu fin_acked ở nhánh này.
5. **Lệnh/khối quan trọng:** Nhánh chủ động tạo CONNECTION_CLOSE application code 0; giữ closing tới min(now + 3*PTO, finish_deadline), có thể resend cùng CLOSE khi peer còn gửi và đã qua PTO. Nhánh peer-close trả thành công mà không gọi finish_close.
6. **Dữ liệu/trạng thái sau bước:** Có thể thành công mà không in QUIC_DRAIN_OK nếu đi qua peer-close. Closing deadline có thể bị cắt ngắn hơn 3 PTO; không khẳng định luôn chờ trọn 3 PTO.
7. **Hàm/sự kiện tiếp:** `quic_client_run()` cleanup.
8. **Lý do chuyển tiếp:** Return 1 của read_packets là sentinel thành công dành riêng cho event_loop; nó không mang nghĩa lỗi nonzero của callback ứng dụng.

### Bước 44 — Giải phóng transport và parser, return về main

1. **Thứ tự / process:** 44; Mỗi process publisher/subscriber.
2. **Hàm/điểm thực thi:** `quic_client_run done → mqtt_app_main → main return`.
3. **File/dòng:** [clients/quic_client.c:571-583](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:571); [clients/app.c:161-170](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:161); [clients/mqtt_codec.c:29](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:29).
4. **Đầu vào và nguồn:** Khởi tạo có thể mới thành công một phần nên cleanup kiểm conn/tls/credentials/fd. rv giữ thành công 0 hoặc lỗi transport; app.parser.len có thể còn byte của packet bị cắt.
5. **Lệnh/khối quan trọng:** Hủy ngtcp2_conn trước TLS vì crypto context còn tham chiếu backend; deinit TLS, free credentials, đóng fd, khôi phục handler, global deinit rồi free c. Caller kiểm parser: nếu transport rc = 0 nhưng len > 0 thì đổi thành lỗi; sau đó free parser.
6. **Dữ liệu/trạng thái sau bước:** c/ring/TLS/fd đã hết vòng đời. app thuộc stack mqtt_app_main, còn role/context thuộc main; chúng hết vòng đời khi hàm chứa tương ứng return. Exit status là 0 khi thành công, 1 cho lỗi runtime; nhánh CLI invalid riêng trả 2 trước khi chạy transport.
7. **Hàm/sự kiện tiếp:** Python Popen.wait quan sát process kết thúc.
8. **Lý do chuyển tiếp:** Transport trả 0 chưa đủ nếu parser còn dữ liệu dang dở; app bổ sung kiểm tra tại biên shutdown.

### Bước 45 — Orchestrator đối chiếu end-to-end và cleanup topology

1. **Thứ tự / process:** 45; Python topology.py / Mininet / OVS.
2. **Hàm/điểm thực thi:** `run_test verify → finally stop_process → main finally`.
3. **File/dòng:** [mininet/topology.py:190-206](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:190); [mininet/topology.py:300-312](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:300); [mininet/topology.py:109-120](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:109).
4. **Đầu vào và nguồn:** Return code hai process, nội dung subscriber log và marker ACK của publisher là đầu vào kiểm tra. owner UUID và gateway_created được giữ từ startup; net quản lý host/link/switch.
5. **Lệnh/khối quan trọng:** Yêu cầu hai exit code bằng 0, có dòng topic/payload mong đợi và marker ACK publisher; chỉ khi đạt hết mới in PASS. finally dừng các process test đã launch nếu còn sống; kiểm owner mn-gw trước del-port; net.stop gọi OwnedBridge.stop kiểm owner; cuối cùng đóng lock fd.
6. **Dữ liệu/trạng thái sau bước:** Topology yêu cầu dọn các tài nguyên thuộc phiên và giữ log. Lock file có thể còn nhưng khóa đã được nhả. Topology không stop broker.
7. **Hàm/sự kiện tiếp:** Process topology trả 0 khi thành công, hoặc 1/130 theo lỗi/signal.
8. **Lý do chuyển tiếp:** PASS dựa trên những điều kiện kiểm tra cụ thể, không phải chỉ vì ping được hoặc dùng một terminal. Cleanup thực tế sau khi thoát vẫn cần bằng chứng kiểm tra mạng/log đúng phạm vi.


## 7. Đối chiếu tổng byte với log lịch sử

Trong kịch bản không có PINGREQ chen vào và dùng default client IDs ngắn:

```text
Publisher stream bytes:
CONNECT (14+n_p) + PUBLISH 39 + DISCONNECT 2 = 55+n_p

Subscriber stream bytes gửi đi:
CONNECT (14+n_s) + SUBSCRIBE 22 + DISCONNECT 2 = 38+n_s
```

Các tổng này đếm **stream bytes gửi đi**, không tính UDP/IP/QUIC overhead, QUIC ACK, bytes nhận hoặc FIN không dữ liệu. Công thức encoder ở [clients/mqtt_codec.c:38-56](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:38) và counters ở [clients/quic_client.c:59-76](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:59), [clients/quic_client.c:174-188](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:174).

Log lịch sử có publisher `acked=76` và subscriber `acked=60`. Nó phù hợp với n_p=21, n_s=22 trong lần chạy đó, không cho biết PID cụ thể. Không được hardcode các số 60/76 vào logic app vì client ID/keepalive/payload có thể thay đổi. [work/mininet-separated-clients/publisher.log:1-9](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/work/mininet-separated-clients/publisher.log:1), [work/mininet-separated-clients/subscriber.log:1-10](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/work/mininet-separated-clients/subscriber.log:1).

## 8. Nếu muốn theo đúng debugger

- Dùng chương10 để đặt breakpoint ở `publisher_connected`, `mqtt_encode_publish`, `quic_client_send`, `flush_packets`, `receive_stream`, `on_data`, `header`, `on_packet`, `subscriber_packet`, `acked_stream`.
- Khi dừng trong subscriber, `user` ở receive_stream là c_s nhưng ở on_data là app_s; xem `c->user`, `app->role_data` thay vì suy từ tên biến.
- Đọc bytes bằng `x/Nbx`, không dùng `x/s` cho vùng binary không có NUL. Quan sát nh/nb/len để hiểu đủ header và đủ packet.
- Kích thước ngtcp2 packet và consumed stream bytes khác nhau; kiểm cả return length và `consumed` tại call site writer.
- Khi sang Linux/OVS/Docker hoặc EMQX, debugger client không có call stack tiếp nối. Chuyển sang capture/process khác và giữ nhãn chứng cứ tương ứng.

Các điểm đặt được lấy từ source đã dẫn trong từng bước; không khẳng định breakpoint đã được chạy hôm nay. Lệnh kiểm tra có thể cần root ở network namespace tương ứng.

[Quay về bản đồ](00-README.md) · [Đối chiếu, giới hạn và lệnh debug](10-TRACEABILITY-AND-GAPS.md)
