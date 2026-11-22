# Đối chiếu source, khoảng trống bằng chứng và cách debug

## 1. Phạm vi kiểm tra và tiêu chuẩn bằng chứng

Ngày phân tích: **10/11/2026 (Asia/Bangkok)**. Đã đọc source C dùng trong chương trình, header của project, Makefile, các script shell/Python điều phối, cấu hình Docker/EMQX, test và log/report liên quan. Dependency ngtcp2 được đối chiếu với header đã cài và source local phiên bản 1.25.0, thay vì suy từ API trên website của phiên bản mới. Version/header/pkg-config: [../demo/src/.deps/include/ngtcp2/version.h:39](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/demo/src/.deps/include/ngtcp2/version.h:39), [../demo/src/.deps/lib/pkgconfig/libngtcp2.pc:31](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/demo/src/.deps/lib/pkgconfig/libngtcp2.pc:31), [/usr/include/gnutls/gnutls.h:52](/usr/include/gnutls/gnutls.h:52), [/usr/lib/x86_64-linux-gnu/pkgconfig/gnutls.pc:20](/usr/lib/x86_64-linux-gnu/pkgconfig/gnutls.pc:20).

Tài liệu mới chỉ nằm trong `docs/code-flow/`. Không sửa source, config, topology, thư viện, executable, certificate, Docker/network state hoặc các ghi chú cũ. Bảng SHA-256 cuối chương làm mốc để đối chiếu source khi số dòng thay đổi.

Nhãn được dùng xuyên bộ: **SOURCE** (code/header/implementation), **CONFIG** (được cấu hình), **HISTORICAL** (log/report lần chạy cũ), **THEORY** (cơ chế hệ điều hành/giao thức), **UNVERIFIED/CHƯA TRIỂN KHAI** (chưa có bằng chứng runtime hoặc không có code tương ứng). SOURCE xác minh luồng điều kiện, không khẳng định một nhánh tùy chọn đã thực sự chạy trong log.

## 2. Ma trận luồng đã truy vết

| Luồng | Điểm vào → điểm ra | Bằng chứng trực tiếp | Tài liệu chi tiết | Phạm vi kết luận |
|---|---|---|---|---|
| Build | root all → clients compile/link pub/sub | [Makefile:1-14](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/Makefile:1); [clients/Makefile:1-35](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/Makefile:1) | 01,00 | SOURCE; không rebuild trong phiên này |
| Broker startup | broker.sh up → cert → Compose wait → listeners | [scripts/broker.sh:1-54](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/scripts/broker.sh:1); [scripts/gen-certs.sh:1-42](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/scripts/gen-certs.sh:1) | 01 | SOURCE/CONFIG; chưa kiểm health hiện tại |
| Subnet guard | options → parse_subnet → inspect → find_conflicts | [scripts/preflight.py:17-112](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/scripts/preflight.py:17); [mininet/topology.py:225-259](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:225) | 01,05 | SOURCE; query live bị hạn chế |
| Topology | lock/name guard → hosts/links/s1/mn-gw → ping | [mininet/topology.py:39-124](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:39); [mininet/topology.py:260-282](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:260) | 01,05 | SOURCE; historical log có ping/app success |
| CLI/test launch | --test h2→SUBACK→h1, hoặc CLI in lệnh | [mininet/topology.py:127-206](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:127); [mininet/topology.py:283-292](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:283) | 01,06,09 | SOURCE; interactive không auto launch |
| App/context startup | role main → mqtt_app_main → quic_client_run | [clients/subscriber.c:113-125](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/subscriber.c:113); [clients/publisher.c:42-49](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/publisher.c:42); [clients/app.c:115-167](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:115) | 01,07 | SOURCE; hai process/context riêng |
| Socket | AF_INET/SOCK_DGRAM → O_NONBLOCK → UDP connect → getsockname | [clients/quic_client.c:212-237](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:212) | 02 | SOURCE; local port/IP runtime không gán sẵn |
| ngtcp2 init | callbacks/settings/params/CIDs/path → ngtcp2_conn_client_new | [clients/quic_client.c:239-282](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:239) | 02 | SOURCE + contract thư viện 1.25.0 |
| TLS/handshake | credentials/CA/SNI/ALPN/session ref → crypto hooks → verify | [clients/quic_client.c:122-143](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:122); [clients/quic_client.c:284-320](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:284) | 02 | SOURCE; historical TLS success có log |
| Stream/MQTT CONNECT | open_stream → on_ready → encode/queue CONNECT | [clients/quic_client.c:338-348](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:338); [clients/app.c:37-47](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:37) | 02,03,04 | SOURCE; QUIC ready và MQTT connected tách biệt |
| Send | role.connected → codec → mqtt_app_send → tx ring → writer → send | [clients/publisher.c:11-19](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/publisher.c:11); [clients/mqtt_codec.c:30-56](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:30); [clients/quic_client.c:63-76](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:63); [clients/quic_client.c:322-394](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:322) | 03 | SOURCE; queue/consumed/send/ACK không đồng nghĩa |
| Receive | poll/recv → ngtcp2_conn_read_pkt → receive_stream → on_data | [clients/quic_client.c:145-172](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:145); [clients/quic_client.c:452-539](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:452); [clients/app.c:66-74](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:66) | 04 | SOURCE + library stream contract |
| Parser/header | chunk bytes → fixed header/body → packet callback | [clients/mqtt_codec.c:59-98](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:59) | 04 | SOURCE; handles split/coalesced packet |
| Session dispatch | CONNACK → connected role; packet sau đó → role.packet | [clients/app.c:49-64](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:49) | 04,06 | SOURCE; không gửi CONNACK thêm vào role.packet |
| Subscriber | SUBACK kiểm ID/QoS → PUBLISH kiểm tra topic → fwrite/count | [clients/subscriber.c:34-83](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/subscriber.c:34) | 04,09 | SOURCE; lịch sử có exact payload |
| Keepalive | tick → PINGREQ → PINGRESP/pending reset | [clients/subscriber.c:73-111](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/subscriber.c:73) | 04,08 | SOURCE; host report lịch sử kiểm PINGRESP thật |
| ACK/retention | ngtcp2 acknowledged stream range → tx_acked → pending | [clients/quic_client.c:174-188](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:174) | 07,08 | SOURCE/library; không phải MQTT PUBACK |
| Loss/timers/CC | expiry APIs + writer scheduling; recovery/CC trong library | [clients/quic_client.c:482-539](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:482); [../third_party/ngtcp2/lib/ngtcp2_settings.c:32-53](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/third_party/ngtcp2/lib/ngtcp2_settings.c:32) | 02,08 | SOURCE; không có measurement cwnd/loss hiện tại |
| Graceful stop | count / QUIC ACK của publish / signal → DISCONNECT → FIN → CLOSE | [clients/app.c:24-34](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:24); [clients/quic_client.c:426-450](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:426); [clients/quic_client.c:482-583](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:482) | 08,09 | SOURCE; hai success-close branches khác nhau |
| Cleanup topology | stop owned children → kiểm tra owner của mn-gw/s1 → net.stop → release lock | [mininet/topology.py:145-153](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:145); [mininet/topology.py:203-206](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:203); [mininet/topology.py:300-312](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:300) | 01,08 | SOURCE; scoped ownership checks |
| End-to-end | h1→broker→h2 topic/payload exact + mã thoát của client | [mininet/topology.py:190-203](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:190); [work/mininet-separated-clients/subscriber.log:1-10](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/work/mininet-separated-clients/subscriber.log:1) | 09 | SOURCE+HISTORICAL; broker internal calls chưa có source |

## 3. Những gì log lịch sử chứng minh và không chứng minh

### 3.1. Mininet test đã lưu

Console log ghi h1=.11, h2=.12, mn-gw=.254, ba phép ping thành công, SUBACK, payload đúng, drain và PASS; xem [work/mininet-separated-clients.log:8-55](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/work/mininet-separated-clients.log:8). Hai client logs ghi handshake TLS1.3/ALPN/certificate verified, CONNECT/CONNACK; publisher có QUIC ACK marker; subscriber có dòng payload và drain. [work/mininet-separated-clients/publisher.log:1-9](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/work/mininet-separated-clients/publisher.log:1), [work/mininet-separated-clients/subscriber.log:1-10](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/work/mininet-separated-clients/subscriber.log:1).

Đây là bằng chứng ứng dụng hoạt động trong lần đã ghi log, cùng logic test yêu cầu mã thoát bằng 0 và payload/ACK marker. Nó không thay thế pcap/rule dump để chứng minh từng lần DNAT/SNAT, MAC, packet number, congestion window, độ trễ từng frame hoặc exact Docker veth. Dòng 1 của console log chỉ là prompt sudo đã được che; tài liệu không chép nội dung đó.

### 3.2. Host integration report

Report ghi rõ scope **host-published-UDP-port; not Mininet h1/h2**, endpoint `127.0.0.1`, ngày 09/11/2026, với 4 case thành công: publish/keepalive, wrong server name, CA không được tin cậy và subscriber nhận SIGINT. [work/host-test-separated-clients/report.json:1-42](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/work/host-test-separated-clients/report.json:1). Code test kiểm PINGRESP marker thật trước publish, không suy từ việc ngủ đủ 10 giây; negative cases yêu cầu fail + certificate evidence và không có CONNACK. [tests/integration.py:107-183](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/tests/integration.py:107).

Không gộp report host thành bằng chứng network namespace h1/h2. Không coi mỗi JSON success là bằng chứng process/container còn sống vào ngày hôm nay.

### 3.3. Unit/lifecycle tests trong source

Codec tests kiểm tra byte vector, topic/UTF-8/ID không hợp lệ, nhiều vị trí chia dữ liệu, feed từng byte và nhiều packet gộp chung. Các case lỗi còn bao gồm Remaining Length, flags, QoS và PUBLISH thiếu dữ liệu. [tests/test_codec.c:24-79](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/tests/test_codec.c:24). Preflight tests dùng dữ liệu giả và lifecycle tests mock shell/interface/OVS side effects, giữ Mininet start/stop dispatch thật. [tests/test_preflight.py:13-46](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/tests/test_preflight.py:13), [tests/test_topology_lifecycle.py:23-81](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/tests/test_topology_lifecycle.py:23).

Test source cho biết kiểm tra gì; trạng thái PASS ngày trước được ghi trong báo cáo cũ. Phiên viết tài liệu không chạy lại `make test` vì không thay đổi code và không cần build lại để kiểm tra Markdown. Không tuyên bố loss/retransmission branch đã được bao phủ end-to-end chỉ từ những test này.

## 4. Khoảng trống runtime và cách kiểm chứng

| Khoảng trống | Điều đã biết | Điều chưa biết | Kiểm tra bổ sung phù hợp |
|---|---|---|---|
| Broker hiện tại | Compose/config đặt image/listener/ports | Container còn running/healthy không, listener có active không | broker.sh ps/listeners/logs; docker inspect |
| IP/bridge/container veth | Compose dùng network driver bridge | IP subnet/bridge name/veth/MAC/ifindex hiện tại | docker network inspect, container PID/netns, ip link/addr |
| NAT/backend | Công bố cổng UDP 14567 đã được cấu hình | Backend iptables/nft, DNAT/SNAT rule và proxy path của gói cụ thể | docker info, firewall read-only dump, conntrack/capture |
| Mininet đang chạy | Topology tạo UUID/lock/owned names | Phiên nào đang giữ tài nguyên, UUID/PID nào còn sống | fuser/lslocks/ovs external_ids/ps |
| ngtcp2 runtime loading | Header/pkg-config phiên bản 1.25.0, ELF dependency/RUNPATH tương ứng | Process cụ thể có bị override loader path không | /proc/PID/maps, ldd/readelf phù hợp, runtime version API nếu instrument sau |
| Broker implementation | Image/tag/config và functional logs | Hàm socket/QUIC/MQTT routing cụ thể trong binary image | Source đúng release/build provenance/symbols của broker |
| Packetization | Writer/parser hỗ trợ arbitrary chunking | Một message lần này nằm trong bao nhiêu UDP packet | pcap + QUIC trace/keylog nếu được bổ sung có chủ đích |
| CID/PN/keys | Library quản lý CID/PN/crypto theo code phiên bản 1.25.0 | Giá trị cụ thể và thời điểm rotate/update ở lần chạy | --verbose log, debugger/library trace có giới hạn |
| Loss/recovery/cwnd | Expiry scheduling và CUBIC mặc định trong source | Số lần mất/retransmit, PTO, cwnd thay đổi thực tế | Thí nghiệm netem/capture/metrics, không suy từ ping không mất gói |
| Cleanup sau một lần chạy | finally và owner checks có code | Nếu kill-9/crash/OVS timeout cụ thể thì tài nguyên nào còn | Kiểm tra hậu trạng thái; không tự xóa hàng loạt |

Trong phiên này, hai phép đọc trạng thái đã thử và không thành công: `ip -j -4 address show` báo **Cannot open netlink socket: Operation not permitted**; `docker inspect emqx-quic` báo **permission denied ... docker.sock**. Đây là hạn chế quan sát từ môi trường thực thi, không phải bằng chứng Docker/Mininet trên máy người dùng đang hỏng. Không dùng mật khẩu/sudo để vượt hạn chế này khi nhiệm vụ chỉ yêu cầu tạo tài liệu.

## 5. Những tính năng chưa triển khai trong nhánh project này

| Tính năng/giả định | Tình trạng theo source | Dẫn chứng/ranh giới |
|---|---|---|
| Server/router/broker C tự viết | Không có trong production files; broker là EMQX image | [clients/Makefile:13-24](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/Makefile:13); [docker/compose.yaml:1-34](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/docker/compose.yaml:1) |
| TCP fallback / MQTT TCP listener dùng trong demo | Không có nhánh fallback; C dùng SOCK_DGRAM, config tắt MQTT TCP/SSL/WS/WSS | [clients/quic_client.c:212-236](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:212); [docker/emqx.conf:12-16](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/docker/emqx.conf:12) |
| HTTP/3 | Không có; ALPN mqtt | [clients/quic_client.c:284-312](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:284) |
| 0-RTT app data | Không triển khai; receive_stream từ chối dữ liệu 0-RTT | [clients/quic_client.c:145-154](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:145) |
| Nhiều application streams / broker-init streams | Client chỉ mở một bidirectional stream; các giới hạn client quảng bá cho stream mới do broker mở bằng 0 | [clients/quic_client.c:263-272](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:263); [clients/quic_client.c:338-347](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:338) |
| Active migration hoàn chỉnh | Client quảng bá `disable_active_migration = 1` cho peer; wrapper không triển khai đổi socket/path chủ động. Không đồng nhất việc này với khả năng xử lý mọi dạng NAT rebinding | [clients/quic_client.c:90-97](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:90); [clients/quic_client.c:270-274](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:270); [clients/quic_client.c:322-336](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:322) |
| Tự điều khiển ECN/path output writer | Writer có output nhưng send_datagram không dùng path/ECN ancillary data | [clients/quic_client.c:374-390](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:374); [clients/quic_client.c:322-336](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:322) |
| PMTUD trong client | settings.no_pmtud=1; max UDP payload 1.200 byte | [clients/quic_client.c:259-265](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:259) |
| Luồng ứng dụng QoS 1 / QoS 2 / PUBACK | Không triển khai trong role hiện tại; PUBLISH QoS 0 | [clients/mqtt_codec.c:49-54](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:49); [clients/mqtt_codec.c:107-113](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:107); [clients/publisher.c:22-39](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/publisher.c:22) |
| Wildcard subscription/multiple topics | Topic validator từ chối ký tự `+` và `#`; encoder/subscriber một topic | [clients/mqtt_codec.c:25-27](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:25); [clients/mqtt_codec.c:44-48](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:44); [clients/subscriber.c:25-26](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/subscriber.c:25) |
| MQTT 5 / khôi phục MQTT session / username-password / Will | Encoder CONNECT MQTT 3.1.1 với clean session, không có các field tương ứng | [clients/mqtt_codec.c:38-43](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:38); [clients/app.c:51-61](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:51) |
| Client certificate/mTLS | Client chỉ load trust CA; broker verify_none | [clients/quic_client.c:284-312](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:284); [docker/emqx.conf:23-30](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/docker/emqx.conf:23) |
| Numeric IPv6 hoặc hostname DNS endpoint | socket IPv4 và inet_pton AF_INET; server_name chỉ danh tính TLS | [clients/quic_client.c:212-236](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:212); [clients/quic_client.c:302-309](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:302) |
| TCLink/default delay/loss trong topology | Dùng Link mặc định; không set netem trong project startup | [mininet/topology.py:122-124](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:122); [mininet/topology.py:262-268](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:262) |
| Qlog/pcap export/metrics dashboard do client tự viết | Không có call/API output tương ứng trong wrapper; chỉ verbose logger | [clients/quic_client.c:205-210](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:205); [clients/quic_client.c:259-265](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:259) |

Đây là mô tả phạm vi lab, không tự mở rộng yêu cầu hoặc sửa implementation. Những thí nghiệm/tính năng mới cần task riêng nếu người dùng muốn phát triển sau khi hiểu luồng hiện tại.

## 6. Phát hiện quan trọng khi đọc code và API

### 6.1. Log “sent” thường đánh dấu queue, chưa chắc socket đã send

`mqtt_app_send` gọi `quic_client_send` để copy byte vào ring, sau đó giải phóng buffer encode. Writer và socket I/O chạy ở giai đoạn flush của outer event loop; queue thành công chưa có nghĩa writer đã chạy. `last_tx` cũng cập nhật ở thời điểm queue thành công, không phải timestamp gói rời NIC. [clients/app.c:13-21](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:13), [clients/quic_client.c:63-76](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:63), [clients/quic_client.c:350-393](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:350). Khi giải thích keepalive/latency phải dùng đúng mốc này.

### 6.2. `tx_sent` là consumed counter và tăng trước syscall send

Writer trả consumed stream bytes và packet length khác nhau. Code tăng tx_sent trước send_datagram; nếu send thất bại thì luồng trả lỗi và đóng, không rollback counter để tự retry app bytes. Retry/loss recovery bình thường là trách nhiệm ngtcp2 khi connection tiếp tục sống. [clients/quic_client.c:377-390](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:377). Không dùng riêng tx_sent để khẳng định kernel/broker đã nhận.

### 6.3. Không gọi ngtcp2 writer từ callback

Callback MQTT có thể encode/queue nhưng không gọi ngtcp2_conn_writev_stream; outer event_loop flush sau đó. Contract dependency có hạn chế reentrancy; thiết kế queue giữ boundary phù hợp. [clients/app.c:13-21](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:13), [clients/quic_client.c:63-76](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:63), [clients/quic_client.c:505-510](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:505), [../demo/src/.deps/include/ngtcp2/ngtcp2.h:5318-5323](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/demo/src/.deps/include/ngtcp2/ngtcp2.h:5318).

### 6.4. CUBIC là default có chứng cứ, không phải phỏng đoán

Project gọi ngtcp2_settings_default mà không override cc_algo; source dependency phiên bản 1.25.0 đặt CUBIC. Điều này nói về client library, không tự suy ra congestion control của EMQX. [clients/quic_client.c:259-264](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:259), [../third_party/ngtcp2/lib/ngtcp2_settings.c:32-53](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/third_party/ngtcp2/lib/ngtcp2_settings.c:32). Chưa có số đo cwnd/runtime congestion trong tài liệu.

### 6.5. Tick 20 ms và retry gửi 1 giây không phải cam kết hard realtime

Poll wait được giới hạn ở 20 ms nhưng callbacks, đọc/gửi nhiều packet và scheduler có thể làm trễ. send_datagram chỉ kiểm deadline 1 giây ở nhánh EAGAIN/EWOULDBLOCK; nhánh EINTR tiếp tục mà không kiểm deadline tại đó. [clients/quic_client.c:322-336](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:322), [clients/quic_client.c:518-530](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:518). Vì vậy comment “At least every 20ms” trong header nên được hiểu cùng implementation thay vì đoán là lịch realtime. [clients/quic_client.h:20-25](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.h:20).

### 6.6. Có hai đường đóng thành công khác nhau

Đường chủ động cần `finishing && tx_fin && fin_acked && pending == 0` và in QUIC_DRAIN_OK. Đường nhận peer DRAINING có error code bằng 0 chỉ cần `finishing && tx_fin && pending == 0`, không kiểm fin_acked riêng và không in marker đó. [clients/quic_client.c:465-468](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:465), [clients/quic_client.c:512-515](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:512). Không đánh fail một lần thành công chỉ vì thiếu marker nếu test không đặt điều kiện đó.

### 6.7. Hold đóng có thể ngắn hơn 3 PTO

`finish_close` tính deadline bằng thời điểm hiện tại cộng 3 PTO, rồi giới hạn deadline đó bởi `finish_deadline`. Signal hoặc socket event còn có thể làm vòng chờ kết thúc sớm. Đây là hành vi thực tế của code, nên không khẳng định wrapper luôn duy trì đủ 3 PTO. [clients/quic_client.c:426-450](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:426). Hành vi chuẩn/thư viện và chính sách của wrapper được tách ở chương 08.

### 6.8. Ngắt kết nối idle/drop phụ thuộc mapping của dependency

Project dùng close_error/make_close chung. Không được kết luận nó gửi CLOSE sai cho IDLE_CLOSE/DROP_CONN chỉ từ việc không có if riêng: `ngtcp2_ccerr_set_liberr` đổi các mã này thành error type riêng (`NGTCP2_CCERR_TYPE_IDLE_CLOSE` hoặc `NGTCP2_CCERR_TYPE_DROP_CONN`). `ngtcp2_conn_write_connection_close_versioned` chỉ tạo packet cho type transport/application; nhánh mặc định trả 0 nên wrapper không có packet CLOSE để gửi trong các trường hợp trên. [clients/quic_client.c:396-424](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:396), [../third_party/ngtcp2/lib/ngtcp2_conn.c:12859-12922](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/third_party/ngtcp2/lib/ngtcp2_conn.c:12859). Chương 08 phân tích đường này và DRAINING.

### 6.9. Ownership của hai bảng callback khác nhau

ngtcp2_callbacks local trong connection_init được constructor copy, nên không dangling khi hàm return. Ngược lại c->callbacks trỏ tới bảng quic_callbacks stack của mqtt_app_main; hợp lệ vì run đồng bộ và caller chưa return. [clients/quic_client.c:239-282](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:239), [clients/quic_client.c:550-552](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:550), [clients/app.c:158-166](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:158), [../third_party/ngtcp2/lib/ngtcp2_conn.c:1460](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/third_party/ngtcp2/lib/ngtcp2_conn.c:1460).

### 6.10. Hàm parser/header và note học cũ cần đọc theo source

Header trả 1 nghĩa đủ fixed header, không có nghĩa đủ body; feed cần len≥nh+nb. cap là tổng capacity, arg chính là app pointer, cb là một function pointer. [clients/mqtt_codec.c:59-98](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:59). Note học cũ hiện có chỗ gọi nhầm argc là argv, dùng argv[1] cho tên executable, struct ví dụ thiếu tên state và tên subscriber_deadline khác source subscribe_deadline. [clients/README.md:4-22](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/README.md:4) so với [clients/subscriber.c:12-18](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/subscriber.c:12). Tài liệu mới giải thích theo C source; không sửa ghi chú cũ trong task này.

### 6.11. Mininet/Docker có tác động dependency ngoài source project

Source project không có lệnh tự flush firewall hoặc thay default route Ubuntu. Tuy nhiên, không nên từ đó kết luận rằng mọi dependency của hệ thống đều không thay cấu hình host. Mininet.init có fixLimits, Docker quản lý published-port/firewall theo backend. Chi tiết và source cài thực tế được chỉ ra ở chương 01/05. [/usr/lib/python3/dist-packages/mininet/net.py:931-937](/usr/lib/python3/dist-packages/mininet/net.py:931), [/usr/lib/python3/dist-packages/mininet/util.py:512-537](/usr/lib/python3/dist-packages/mininet/util.py:512), [docker/compose.yaml:11-13](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/docker/compose.yaml:11).

### 6.12. Một số API timer đang deprecated nhưng vẫn tồn tại trong version dùng

Project gọi ngtcp2_conn_get_expiry và ngtcp2_conn_get_pto; header phiên bản 1.25.0 giữ chúng và chỉ dẫn API *2 mới hơn. Đây không phải lỗi build do thiếu symbol, cũng không là yêu cầu tự sửa code trong task tài liệu. [clients/quic_client.c:430-434](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:430), [clients/quic_client.c:501-521](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:501), [../demo/src/.deps/include/ngtcp2/ngtcp2.h:4678-4736](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/demo/src/.deps/include/ngtcp2/ngtcp2.h:4678).

## 7. Breakpoint theo câu hỏi nghiên cứu

Trong bảng dưới, **Q = `tx_queued`**, **S = `tx_sent`**, **A = `tx_acked`**. Đây là các mốc byte stream tuyệt đối, không phải số packet; Q tăng khi copy vào queue, S tăng theo `consumed` của ngtcp2 và A tăng qua callback ACK. [clients/quic_client.c:59–75](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:59), [clients/quic_client.c:174–186](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:174), [clients/quic_client.c:386–388](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:386).

| Câu hỏi | Breakpoint project | Giá trị xem khi đang ở đúng scope | Bằng chứng vị trí |
|---|---|---|---|
| CLI đổi defaults thế nào? | mqtt_app_main, các case getopt | options, app, optarg, optind, role/role_data | [clients/app.c:115-155](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:115) |
| FD/endpoint từ đâu? | socket_open sau getsockname | c->fd, c->local, c->remote | [clients/quic_client.c:212-236](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:212) |
| CIDs/settings/callbacks tạo thế nào? | connection_init trước constructor | dcid/scid, settings, params, path, c | [clients/quic_client.c:239-280](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:239) |
| App bắt đầu được gửi khi nào? | handshake_completed, open_stream, on_ready | c.handshake/ready/stream_id, app.connect_sent/deadline | [clients/quic_client.c:122-143](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:122); [clients/quic_client.c:338-347](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:338); [clients/app.c:37-47](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:37) |
| PUBLISH từ chuỗi thành byte? | publisher_connected, mqtt_encode_publish | app.message/topic; n/nt; b.data/b.len | [clients/publisher.c:11-19](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/publisher.c:11); [clients/mqtt_codec.c:49-54](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:49) |
| Buffer encoder có còn sống không? | mqtt_app_send, quic_client_send | buffer.data/len; c.tx và Q/S/A | [clients/app.c:13-21](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:13); [clients/quic_client.c:63-76](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:63) |
| Một writer consume bao nhiêu? | flush_packets sau call writer | length, consumed, vectors, flags, stream id, Q/S/A | [clients/quic_client.c:350-390](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:350) |
| Byte QUIC thành byte MQTT ở đâu? | receive_stream, on_data | conn/user/c.user; id/offset/flags/data/length | [clients/quic_client.c:145-172](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:145); [clients/app.c:66-74](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:66) |
| Header đầy đủ khác packet đầy đủ thế nào? | header, mqtt_parser_feed | n, p bytes, nh/nb/hr, parser.len/cap | [clients/mqtt_codec.c:59-98](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c:59) |
| Context app hay subscriber? | on_packet, subscriber_packet | app, app.role_data, packet/body, state/received | [clients/app.c:49-64](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:49); [clients/subscriber.c:34-83](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/subscriber.c:34) |
| Vì sao chưa đóng dù đã send? | acked_stream, publisher_tick | Q/S/A, pending, publish_deadline, now | [clients/quic_client.c:174-188](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:174); [clients/publisher.c:29-39](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/publisher.c:29) |
| FIN/peer close success branch? | flush_packets, read_packets, finish_close | finishing/tx_fin/fin_acked/rx_fin; peer error; deadline/PTO | [clients/quic_client.c:350-450](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:350); [clients/quic_client.c:465-468](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:465) |

Ví dụ lệnh GDB sau khi người đọc đã xác định đúng PID process subscriber đang chạy:

```text
sudo gdb -p PID_SUBSCRIBER
break receive_stream
break on_data
break mqtt_parser_feed
break header
break on_packet
break subscriber_packet
continue
```

`PID_SUBSCRIBER` là chỗ điền PID thực tế, không phải chuỗi đưa nguyên cho shell. Đối chiếu lệnh process trước khi attach. Với binary build `-g`, GDB có thể đọc type/local theo optimization hiện tại; -O2 có thể làm một số biến optimized out. Flags build ở [clients/Makefile:9-10](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/Makefile:9).

Khi dừng ở on_data:

```text
p length
p (mqtt_app *)user
p ((mqtt_app *)user)->parser
x/4bx data
bt
```

Chỉ dùng `x/4bx` khi `length >= 4`; nếu ngắn hơn, đọc đúng số byte hợp lệ. Ở feed/header, `p` mang kiểu khác nhau; phải nhìn frame/signature hiện tại, không dùng cùng biểu thức ở mọi frame. Ở receive_stream, user là c; ở on_data, user là app. [clients/quic_client.c:145-156](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:145), [clients/app.c:66-69](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:66).

Nếu muốn breakpoint vào public API ngtcp2, lưu ý tên call trong source là macro wrapper; symbol thật có hậu tố `_versioned`. Ví dụ `ngtcp2_conn_read_pkt_versioned`, `ngtcp2_conn_writev_stream_versioned`, `ngtcp2_conn_client_new_versioned`. [../demo/src/.deps/include/ngtcp2/ngtcp2.h:7299-7305](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/demo/src/.deps/include/ngtcp2/ngtcp2.h:7299), [../demo/src/.deps/include/ngtcp2/ngtcp2.h:7327-7337](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/demo/src/.deps/include/ngtcp2/ngtcp2.h:7327), [../demo/src/.deps/include/ngtcp2/ngtcp2.h:7389-7400](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/demo/src/.deps/include/ngtcp2/ngtcp2.h:7389).

Khi debugger dừng process, đồng hồ monotonic của hệ thống vẫn tiếp tục chạy; peer và các timer có thể timeout trước hoặc ngay khi process được tiếp tục. Đây là tác động thực nghiệm cần ghi khi giải thích log, không phải phép đo mạng tự nhiên của demo.

## 8. Lệnh quan sát mạng/process để người đọc kiểm tra

Đây là lệnh gợi ý, không phải kết quả đã chạy thành công trong môi trường viết tài liệu. Thực hiện ở máy người dùng trong lúc topology đúng phiên đang sống; các thao tác bên dưới chỉ đọc trạng thái, ngoại trừ debugger dừng/tiếp tục process như đã giải thích.

### 8.1. Trong Mininet CLI

```text
nodes
net
dump
h1 ip -4 address show dev h1-eth0
h2 ip -4 address show dev h2-eth0
h1 ip -4 route show
h2 ip -4 route show
h1 ip route get 10.78.0.254
h2 ss -uapn
h1 ip neigh show
h2 ip neigh show
sh ps -C subscriber,publisher -o pid,ppid,comm,args
```

Tiền tố `h1`/`h2` chọn môi trường mạng thực thi lệnh. `ps` không tự lọc process theo network namespace: nhánh Mininet đang dùng tạo shell host bằng `mnexec` với tùy chọn network namespace, không tạo PID namespace riêng. Xem [Node.startShell:147–168](/usr/lib/python3/dist-packages/mininet/node.py:147) và phân tích dependency ở chương 01/05. Lệnh `ss` quan sát socket theo network namespace nơi nó được thực thi.

### 8.2. Trên Ubuntu host

```bash
sudo ovs-vsctl show
sudo ovs-vsctl list-ports s1
sudo ovs-vsctl get Bridge s1 external_ids:mqtt-quic-owner
sudo ovs-vsctl get Interface mn-gw external_ids:mqtt-quic-owner
ip -4 address show dev mn-gw
ip -4 route show table all
sudo fuser -v /run/lock/mqtt-quic-mininet.lock
sudo lslocks -o PID,COMMAND,PATH
```

File lock tồn tại không đồng nghĩa khóa đang giữ. fuser chỉ cho biết process mở file; lslocks và commandline/PID mới giúp đối chiếu phiên. Ownership UUID là cơ chế source ở [mininet/topology.py:39-63](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:39), [mininet/topology.py:97-120](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:97), [mininet/topology.py:271-274](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py:271).

### 8.3. Broker và Docker network

```bash
/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/scripts/broker.sh ps
/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/scripts/broker.sh listeners
/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/scripts/broker.sh logs
docker inspect emqx-quic
docker network ls
docker top emqx-quic
```

Sau khi tìm đúng tên network từ inspect, dùng `docker network inspect TEN_NETWORK_THUC_TE`. Không điền IP bridge lịch sử vào cấu hình mới chỉ vì báo cáo cũ từng dùng `172.19.0.0/16`. Compose driver bridge không cố định IPAM subnet ở source. [docker/compose.yaml:27-34](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/docker/compose.yaml:27).

### 8.4. Capture và đọc cấu hình firewall

```bash
sudo tcpdump -ni mn-gw udp port 14567
sudo iptables -t nat -S
sudo iptables -nvL DOCKER-USER
sudo nft list ruleset
```

Chọn công cụ phù hợp backend thật; một chain không tồn tại không tự chứng minh traffic bị chặn. Không flush ruleset hoặc thêm NAT để “thử” khi chưa có chứng cứ. Để so ingress/egress cần capture ở nhiều interface thực tế, đối chiếu timestamp/tuple/connection ID với giới hạn QUIC encryption. `tcpdump` không tự giải mã payload MQTT của kết nối này vì dữ liệu ứng dụng được QUIC bảo vệ.

## 9. Câu hỏi kỹ thuật còn mở cho thực nghiệm sau

1. Docker backend/version nào đang áp dụng; UDP port publication dùng DNAT/conntrack/proxy cụ thể nào đối với nguồn h1/h2?
2. Tên bridge/veth và MAC/IP container hiện tại là gì; reply qua các hook nào?
3. Source package/commit của broker image 6.3.2 là gì; có symbol/trace để mở rộng hàm nội bộ QUIC và routing MQTT không?
4. Khi thêm loss/delay trên một hay cả hai nhánh, PTO/cwnd/stream ACK tiến thế nào; timeout app hay test orchestration chạm trước?
5. Các nhánh Retry / key update / PATH_CHALLENGE có thực sự xuất hiện với broker/config này không?
6. Thời gian chờ đóng bị rút ngắn bởi deadline trong trường hợp RTT/PTO cao ảnh hưởng thực tế thế nào? Cần phép đo mới trước khi đề nghị thay chính sách.
7. Nếu cần hỗ trợ payload lớn sát giới hạn, queue outstanding CONNECT/PING có ảnh hưởng dung lượng queue còn trống ra sao; có nên stream/chunk ở tầng app sau này không?

Các câu hỏi là khoảng trống đo lường hoặc phát triển, không được trình bày như tính năng đã có. Source giới hạn queue/counters tại [clients/quic_client.c:59-76](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c:59) và app timeout tại [clients/app.c:76-88](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c:76).

## 10. Mốc source để bảo toàn truy vết

SHA-256 dưới đây được lấy trước khi viết bộ tài liệu. Mục tiêu là nhận diện phiên source mà số dòng dựa vào. Nếu code thay đổi, cần đối chiếu lại citation và sơ đồ, không giữ số dòng cũ một cách máy móc. Không đưa nội dung private key/certificate hoặc mật khẩu vào manifest.

| File | Số dòng | SHA-256 |
|---|---:|---|
| [DE_XUAT.md](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/DE_XUAT.md) | 35 | `d71f88f8852fedad205879ed53526e6f8addca650618dd802d3e468739e1b07f` |
| [Makefile](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/Makefile) | 14 | `99c5747eaae068ffec5b5f8c78a0d66fe37b87c352ec377f2bdb0804398442cf` |
| [README.md](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/README.md) | 247 | `494af1df07eec87aaa95b7e05849d758d10c69a8ecd5ee0ff54b7b94df87f67d` |
| [clients/Makefile](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/Makefile) | 35 | `b39ed4c2ffcb5a254c964a816a681463da1370b527ff5b991053949e2239836c` |
| [clients/README.md](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/README.md) | 54 | `0ed0486435e900ae3a543557815c46bed5a39dfe5401d7a7ce06eb3a09c4f618` |
| [clients/app.c](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.c) | 171 | `3cb128b2e7a01a37dd4ea73957433907490613252b3fccce1ff53304f480e1a0` |
| [clients/app.h](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/app.h) | 35 | `4effbfc4d9a34cb38c48e4125b9b0c60cb92deb0a6871f808b4536a292e9937c` |
| [clients/mqtt_codec.c](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.c) | 113 | `4a1d1505ef301335124fdf6dcfc766830c19f5f0213e5cddaa17abcbf18a7dac` |
| [clients/mqtt_codec.h](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/mqtt_codec.h) | 34 | `6e9fae3a8a0bd86d39cad0d5ae2faefa511c2e3809054145682705a11da7ba77` |
| [clients/publisher.c](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/publisher.c) | 49 | `b14a6b920ca0c5936223fd4aa875c5603d452effc11cb6dcd9780256bdb0345f` |
| [clients/quic_client.c](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.c) | 583 | `8f43588d7a26ef82d0dcc806d1e235301f40f44c4e3b221eff3e67b514e554c1` |
| [clients/quic_client.h](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/quic_client.h) | 43 | `6a6caf4069be72685851f8bd7b0a593d87ddbd23b1592d3f099052f4830a0699` |
| [clients/subscriber.c](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/clients/subscriber.c) | 125 | `81b4c9715d649a07524d561fe6d221043f183e39ab4792282ac37b813da6981f` |
| [docker/compose.yaml](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/docker/compose.yaml) | 34 | `b0fb63816d32961650baee5cfac9d14a1134105b230608927548db299fbc8b39` |
| [docker/emqx.conf](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/docker/emqx.conf) | 33 | `40091984800709329cd2dd89f91bc852fee212a0e9c76f052380288e01504931` |
| [docs/HUONG_DAN_HOC_CODE.md](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/docs/HUONG_DAN_HOC_CODE.md) | 696 | `b6634838baa66dcb99b3b7c4c47a36518e42bf140945ae719120eed8e8e78267` |
| [docs/KET_QUA_KIEM_THU.md](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/docs/KET_QUA_KIEM_THU.md) | 157 | `1f092a01320a5d204816caac57863845df582aed18a3cb60e3164d66fb218a7d` |
| [mininet/topology.py](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/mininet/topology.py) | 316 | `10e2f62cc7cf3aad80057d8d9970fa612e9c95e142536b912095710458bc8639` |
| [scripts/broker.sh](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/scripts/broker.sh) | 54 | `0a0953288fe570b5794d4b2bb0e13f6c0bcff69ae9cf9fadc2789cad32563ce1` |
| [scripts/gen-certs.sh](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/scripts/gen-certs.sh) | 42 | `f871181c89aeec0c9e56aa70e0ae53d89ad011395a02760e8a150a80710d4da1` |
| [scripts/preflight.py](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/scripts/preflight.py) | 116 | `1af6b9d78f41ed591e9048bf4941801d802d0704f32bc229d81ca122cf61c999` |
| [tests/integration.py](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/tests/integration.py) | 201 | `3e755c34819936bb437dd86075fdd1fc0946d784ac4220dbdf6d535e39d290f5` |
| [tests/test_codec.c](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/tests/test_codec.c) | 79 | `aa7741e1e82eb0723726c74443e59ad78f5dfb92027d5ab5f51e116bc2c45d7d` |
| [tests/test_preflight.py](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/tests/test_preflight.py) | 50 | `21249cc1f3233d0b1ddea862839b7d6bc801861b912fcfc813c7f13cec2473e9` |
| [tests/test_topology_lifecycle.py](/home/nguyenluongduc/Github/QUIC/ZerotoHeroWithQUIC/src/PrgSocket/QUICSocket/mqtt-quic/tests/test_topology_lifecycle.py) | 85 | `c6028df420f2f7ed07242cd6dd3d74512620b50e400588dae7fc182f770a0ff7` |


## 11. Kiểm tra chính bộ tài liệu

Trước khi chuyển bộ tài liệu vào project, các đường dẫn/source-line được kiểm tra tồn tại và nằm trong phạm vi file; liên kết điều hướng giữa các chương phải trỏ đúng danh sách 11 file. Các code fences/Mermaid blocks được kiểm tra đóng đủ; nội dung sơ đồ được đối chiếu thủ công với call sites và điều kiện trong source. Không coi chỉ kiểm tra số dòng là đã chứng minh một claim được đoạn đó hỗ trợ: các nhánh quan trọng được đọc lại cùng header/implementation dependency.

Mermaid engine có thể khác giữa các trình xem Markdown. Nếu môi trường không có parser/renderer Mermaid cài sẵn, không tự tải package chỉ để thay đổi môi trường; ghi rõ mức kiểm tra cú pháp thực hiện trong biên bản ở chương 00 hoặc báo cáo bàn giao. Sơ đồ luôn có diễn giải chữ nên không phụ thuộc hoàn toàn vào khả năng render của trình đọc.

Tính đầy đủ được đối chiếu qua 11 chương bắt buộc: call graph/registry liệt kê các hàm C của chương trình và các điểm đăng ký callback; startup/network dẫn tới entry point thật; walkthrough gồm 45 bước ghi process, nguồn đầu vào, thay đổi trạng thái, bước tiếp theo và lý do chuyển tiếp. Các chương cũng theo dõi đường return khi gặp lỗi và cleanup, đồng thời chỉ rõ ranh giới nơi code broker/kernel nằm ngoài source đang có.

[Quay về bản đồ và thứ tự đọc](00-README.md) · [Đọc lại walkthrough](09-END-TO-END-WALKTHROUGH.md)
