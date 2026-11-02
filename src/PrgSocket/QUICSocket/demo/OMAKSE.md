# 1. Testbed.h

## Định nghĩa các kiểu dữ liệu, khai báo các hàm để những phần khác của chương trình cùng sử dụng.

- Với define QUIC_TESTBED_H, nó sẽ sử dụng các thư viện có sẵn như:
  - ngtcp2/ngtcp2.h: chứa các kiểu và API chính của QUIC như: connection, stream, CIO, settings, transport parameters.
  - ngtcp2/ngtcp2_crypto.h: giúp hỗ trợ phân tích mật mã/TLS với ngtcp2 (ngtcp2_crypto_conn_ref)
  - ngtcp2/ngtcp2_crypto_gnutls: Các hàm cấu hình GnuTLS để làm TLS backend cho ngtcp2
  - gnutls/crypto.h: API mật mã của GnuTLS
  - netinet/in.h: kiểu địa chỉ mạng IPv4
  - signal.h: Các kiểu và khai báo dùng khi xử lý tín hiệu nhưu Ctrl+C
  - stdbool.h: bool, true, false trong chế độ C++ của chương trình
  - stdint.h: các kiểu số nguyên có độ rộng xác định nhưu uint8_t, uint64_t, int64_t
  - stdio.h: các hàm xuất nhập như FILE, printf(), và size_t
- Hằng số giới hạn dữ liệu:
  - #define TB_MAX_DATA 65536u (u là hậu tố chỉ một hằng số nguyên không dấu): Giới hạn gửi dữ liệu của ứng dụng testbed hiện tại. Tuy nhiên muốn thay đổi cần kiểm tra lại giới hạn được công bố trong transport parameters.
  - #define TB_ALPN "quic-testbed/1" (Application-Layer Protocol Negotiation): thống nhất giao thức ứng dụng trong quá trình TLS handshake
  - #define TB_TLS_PRIORITY "NORMAL:-VERS-ALL:+VERS-TLS1.3:%DISABLE_TLS13_COMPAT_MODE": Đơn giản chỉ là sử dụng TLS phiên bản TLS 1.3
- Cấu trúc tb_options (thông tin cấu hình):
  - const char (dùng con trỏ ký tự, dùng để tham chiếu tới chuỗi):
    - \*host: Server dùng làm địa chỉ để bind, Client dùng để làm địa chỉ đích
    - \*port: bind UDP socket vào port để nhận packet hoặc gửi packet từ client tới port đó trên máy server
    - \*cert: Đường dẫn tới chứng chỉ của Server
    - *key: Đường dẫn tới chứng chỉ private key của Server 
    - *ca: Đường dẫn tới chứng chỉ hoặc tập chứng chỉ mà Client dùng để kiểm tra Server
    - \*server_name: Danh tính DNS/IP mà client cần đối chiếu với chứng chỉ server (giả sử localhost).
    - \*message: Thông điệp gửi đi
    - \*input: Đường dẫn gửi đi
    - \*output: Đường dẫn để lưu
  - unsigned int timeout (s): thời gian tối đa chương trình chờ hoặc cho phép một lượt trao đổi dữ liệu.
  - bool verbose: biểu diễn true/false để quyết định có bật log chi tiết của ngtcp2 hay không
- Cấu trúc tb_endpoint: trạng thái của một đầu kết nối
  - bool server: true -> trạng thái server, false -> trạng thái client
  - int fd: file descriptor của UDP socket
  - struct sockaddr_in:
    - local: địa chỉ IP và Port của phía đang chạy
    - remote: địa chỉ IP và Port của phía còn lại
  - ngtcp2_conn \*quic: ngtcp2_conn trường này chứa địa chỉ của đối tượng chứa các trạng thái của một connection bao gồm: trạng thái handshake, stream, ack, timer, recovery trạng thái truyền.
  - ngtcp2_crypto_conn_ref conn_ref: giúp phần tích hợp TLS tìm lại QUIC connection tương ứng:
  ```text
  typedef struct ngtcp2_crypto_conn_ref {
      ngtcp2_crypto_get_conn get_conn;
      void *user_data;
  } ngtcp2_crypto_conn_ref;
  ```
    - ngtcp2_crypto_get_conn là con trỏ hàm: đưa hàm tới đối tượng tham chiếu, hàm trả lại con trỏ tới QUIC connection, tức là: Đặt tên cho con trỏ hàm là ngtcp2_crypto_get_conn, con trỏ hàm này phải trỏ tới hàm nhận giá trị ngtcp2_crypto_conn_ref* conn_ref và phải trả về kiểu ngtcp2_conn* 

  ```text
      typedef ngtcp2_conn *(*ngtcp2_crypto_get_conn)(
          ngtcp2_crypto_conn_ref *conn_ref);
  ```
    - void *user_data: ở đây sẽ chứa địa chỉ của end_point
  - Tóm lại, cấu trúc này này sẽ giúp các hàm khác lấy được địa chỉ của trạng thái kết nối để xử lý, cấu trúc này chứa trường con trỏ hàm ngtcp2_crypto_get_conn get_conn nhằm tìm lại được địa chỉ của trạng thái kết nối,trường con trỏ dữ liệu void *user_data chứa địa chỉ của end_point (để lấy địa chỉ của trạng thái kết nối của end_point đó).
  - gnutls_session_t tls: là đối tượng trong bộ nhớ, giúp thư viện thực hiện và theo dõi trạng thái TLS cho một lượt kết nối mới
    - Nó cần thiết bởi vì client và server cần handshake và 2 bên cần trao đổi thông điệp và xử lý trong nhiều bước.
    - Chẳng hạn Client chuẩn bị thông điệp handshake đầu tiên -> Client gửi nó cho Server thông qua QUIC -> Server nhận được và xử lý -> Server tạo phản hồi và gửi -> Client nhận phản hồi và xử lý phản hồi -> Hai bên hoàn thành handshake.
    - Như vậy client cần phải nhớ được rằng (phía Server cũng phải nhớ được khóa tương ứng của nó):
      - Đang ở bước nào?
      - Đang dùng thuật toán mã hóa nào
      - Nhận được những thông tin gì từ Server
      - Chứng chỉ Server có hợp lệ hay không
      - Những khóa nào đã được dẫn xuất 
  - gnutls_certificate_credentials credentials: Trong chương trình, Server cần chứng chỉ để cho Client và private key tương ứng để chứng minh quyền sở hữu khóa, còn Client cần các chứng chỉ tin cậy để kiểm tra Server. Trường này được sử dụng để quản lý những thông tin này, nó sẽ giữ địa chỉ kết nối cảu đối tượng chứa các thông tin này.

  - int tb_parse_option(tb_option *o, bool server, int argc, char **argv): hàm này sẽ điền các giá trị vào tb_option, char **argv là con trỏ cấp 2 vì cần cập nhật *message.
  - int tb_endpoint_init(tp_endpoint *ep, const tb_option *o, bool server): Hàm này sẽ khởi tạo trạng thái ban đầu của một endpoint, cập nhật các trường của endpoint
  - void tb_endpoint_free(tb_endpoint *ep): thu hồi tài nguyên của endpoint
  - int tb_socket_open(tb_endpoint *ep): tạo và chuẩn bị UDP socket
  - int tb_socket_connect(tb_endpoint *ep): chọn peer cho UDP socket, gọi connect cho UDP socket
  - ntcp2_path tb_path(tb_endpoint *ep): tạo thông tin đường truyền cho ngtcp2, bao gồm địa chỉ local, địa chỉ remote
  ```text 
  - void tb_quic_config(tb_endpoint *ep, ngtcp2_callbacks *cb,
                    ngtcp2_settings *settings, ngtcp2_transport_params *params);
  ```
  => chuẩn bị về callback, settings và transport
  - int tb_random_cid(ngtcp2_cid *cid, size_t length): sinh các CID ngẫu nhiên
  - int tb_tls_finish(tb_endpoint *ep): hoàn thiện phần cấu hình TLS session và liên kết các các đối tượng đã chuẩn bị
  - int tb_read_packet(tb_endpoint *ep, const uint8_t *data, size_t length): Đưa packet được nhận vào ngtcp2 để xử lý
  - int tb_event_loop(tb_endpoint *ep): chạy vòng lặp điều phối connection
  - void tb_send_error(tb_endpoint *ep, int liberr): gửi thông tin đóng connection do lỗi
  - void tb_print_stats(tb_endpoint *ep, int result): in số liệu của lượt chạy
  - int tb_load_payload(tb_endpoint *ep): nạp dữ liệu ứng dụng cần gửi
  - int tb_save_echo(tb_endpoint *ep): lưu echo ra file
  