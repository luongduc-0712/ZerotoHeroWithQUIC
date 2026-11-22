# 1. Subscriber
- Hàm main gồm 2 đối số: agrc và agrv
    - Đối số:
        - argv (argument count): là số lượng đối số. Chẳng hạn chạy lệnh ./clients/subscriber --host 127.0.0.1 --count 1 thì **argc = 5**.
        - argv (argument vector): giữ các chuỗi tương ứng của argc (bao gồm cả kết thúc chuỗi '\0'). Chẳng hạn "./clients/subscriber" có địa chỉ là argv[1], thì ở main sẽ khai báo là char **argv = char *argv[]
    - subscriber_context subscriber = {.state = WAIT_SUBACK}: tức là khởi tạo một biến subscriber có kiểu subscriber_context, rồi khởi tạo trạng thái ban đầu của nó.
        - Trạng thái ban đầu ở đây đặt là WAIT_SUBACK, có thể đặt là INIT cho dễ hiểu.
        ```text
        enum subscriber_state {
            WAIT_SUBACK = 2,
            SUBSCRIBED = 3
        };
        ```
        - Một subscriber cần ghi nhớ: subscriber đang ở trạng thái nào (chờ broker xác nhận đăng ký hay broker đã chấp nhận đăng ký), đã nhận bao nhiêu message, có đang chờ phản hồi ping không, đã gửi ping vào lúc nào, chờ suback tới thời điểm nào
        ```text
        typedef struct {
            enum subscriber_state;
            unsigned received; (số nguyên không dấu, lưu số không âm)
            int ping_pending; (0: không có PINGRESP; 1: đã gửi PINGREQ và chờ PINGRESP)
            uint64_t ping_at; (biết xem đã chờ PINGRESP đã lâu hay chưa)
            uint64_t subscriber_deadline; (ghi lại thời điểm hết hạn chờ một việc)
        } subscriber_context;
        ```
    - mqtt_app app: giữ trạng thái phiên mqtt của client:
        ```text
        struct mqtt_app {
            quic_client *quic;
            mqtt_parser parser;
            const char *topic, *message, *client_id;
            unsigned count, timeout;
            uint64_t started, deadline, last_tx;
            int connect_sent, connected, stopping;
            const mqtt_role *role;
            void *role_data;
        };
        ```
        - parser để ghép các byte nhận được thành gói MQTT
        - topic: để xác nhận kênh gửi và nhận tin
        - client_id: xác định danh tính của client trong gói MQTT CONNECT
        - cờ app->connected: 1 nếu broker đã chấp nhận gói CONNECT qua CONNACK
        - app->quic: là đối tượng chứa socket, trạng thái kết nối QUIC, TLS, các con trỏ đến options (cấu hình QUIC: id, port, server_name, ca,...), callbacks (hàm xử lý sự kiện của của QUIC)
        - app->role: chứa địa chỉ trỏ tới bảng mqtt_role, gồm cấu hình và cách callbacks xử lý
        - app->role_data: giữ subscriber_context subscriber: chứa trạng thái của subscriber
    - mqtt_role role: chuẩn bị một bảng để mô tả subscriber (định nghĩa đây là subscriber), cấu hình mặc định của nó và những hàm cần gọi khi có sự kiện. Tóm lại là mô tả chương trình sẽ cần xử lý sự kiện như thế nào.
        ```text
        const mqtt_role role = {
            .name = "subscriber",
            .subscriber = 1,
            .default_timeout = 0,
            .connected = subscriber_connected,
            .packet = subscriber_packet,
            .tick = subscriber_tick
        };
        ```
        - Kiểu struct mqtt_role: 
            ```text
            typedef struct {
                const char *name;
                int subscriber; /* Chỉ chọn các tùy chọn CLI --count hoặc --message. */
                unsigned default_timeout;
                int (*connected)(mqtt_app *);
                int (*packet)(mqtt_app *, const mqtt_packet *);
                int (*tick)(mqtt_app *, uint64_t now);
            } mqtt_role;
            ```
    - return mqtt_app_main(argc, char **argv, &role, &subscriber)
        - &subscriber: địa chỉ đối tượng chứa trạng thái và một số điều kiện của subscriber
        - quic_option: địa chỉ của đối tượng chứa thông tin mặc định cần dùng để kết nối QUIC
        - Tạo MQTT client ID: Client ID là một chuỗi định danh mqtt của client
        - Tạo một đối tượng session dùng chung mqtt_app app
        - Khai báo các tùy chọn mà chương trình hiểu: tức là chương trình chỉ chấp nhận các tùy chọn trong danh sách đã khai báo khi thực thi chương trình
        - Vòng lặp đọc cấu hình vừa khai báo ở trên và cập nhật cấu hình
        - Callbacks:
            - on_data: nhận các byte ứng dụng do lớp QUIC giao lên, đưa vào MQTT parser và báo lại thành công hoặc thất bại

    - Các công việc còn lại:
        - callback + mqtt
        - hàm thực hiện trong subcriber
        - publisher
        - cách ngtcp2 thực thi luồng dữ liệu