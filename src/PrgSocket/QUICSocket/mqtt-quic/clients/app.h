#ifndef MQTT_QUIC_APP_H
#define MQTT_QUIC_APP_H

#include "mqtt_codec.h"
#include "quic_client.h"

typedef struct mqtt_app mqtt_app;

/* Mỗi executable tự định nghĩa hành vi MQTT sau CONNACK thành công. */
typedef struct {
    const char *name;
    int subscriber; /* Chỉ chọn các tùy chọn CLI --count hoặc --message. */
    unsigned default_timeout;
    int (*connected)(mqtt_app *);
    int (*packet)(mqtt_app *, const mqtt_packet *);
    int (*tick)(mqtt_app *, uint64_t now);
} mqtt_role;

/* Trạng thái publish/subscribe riêng nằm trong role_data. */
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

/* Gửi bản sao vào hàng đợi QUIC, giải phóng buffer và cập nhật last_tx. */
int mqtt_app_send(mqtt_app *, mqtt_buffer *, int encoding_result);
int mqtt_app_disconnect(mqtt_app *);
int mqtt_app_main(int argc, char **argv, const mqtt_role *, void *role_data);
#endif
