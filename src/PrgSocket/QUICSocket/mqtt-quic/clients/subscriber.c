#include "app.h"

#include <stdio.h>
#include <string.h>

/* Hai trạng thái của subscriber sau khi MQTT CONNECT đã được chấp nhận. */
enum subscriber_state {
    WAIT_SUBACK = 2,
    SUBSCRIBED = 3
};

typedef struct {
    enum subscriber_state state;
    unsigned received;
    int ping_pending;
    uint64_t ping_at;
    uint64_t subscribe_deadline;
} subscriber_context;

static int subscriber_connected(mqtt_app *app) {
    subscriber_context *subscriber = app->role_data;
    mqtt_buffer buffer = {0};

    /* Sau CONNACK hợp lệ, đăng ký một topic với packet ID 1 và QoS 0. */
    int result = mqtt_encode_subscribe(&buffer, 1, app->topic);
    if (mqtt_app_send(app, &buffer, result)) return -1;

    subscriber->state = WAIT_SUBACK;
    subscriber->subscribe_deadline = quic_client_now_ms() + 15000;
    printf("MQTT SUBSCRIBE sent topic=%s\n", app->topic);
    return 0;
}

static int subscriber_packet(mqtt_app *app, const mqtt_packet *packet) {
    subscriber_context *subscriber = app->role_data;

    if (packet->type == 9 && subscriber->state == WAIT_SUBACK) {
        uint16_t packet_id;
        uint8_t granted_qos;
        if (mqtt_decode_suback(packet, &packet_id, &granted_qos) ||
            packet_id != 1 || granted_qos != 0) {
            fprintf(stderr, "MQTT SUBACK invalid or subscription rejected\n");
            return -1;
        }
        subscriber->state = SUBSCRIBED;
        puts("MQTT SUBACK accepted");
        return 0;
    }

    /* Chỉ nhận PUBLISH QoS 0 trên đúng topic sau khi broker chấp nhận đăng ký. */
    if (packet->type == 3 && subscriber->state == SUBSCRIBED) {
        mqtt_publish message;
        if (mqtt_decode_publish(packet, &message) ||
            strlen(app->topic) != message.topic_len ||
            memcmp(app->topic, message.topic, message.topic_len)) {
            fprintf(stderr, "MQTT PUBLISH malformed or unexpected topic/QoS\n");
            return -1;
        }

        /* Payload có thể chứa byte NUL, vì vậy ghi theo độ dài thay vì dùng %s. */
        fputs("MQTT PUBLISH received topic=", stdout);
        fwrite(message.topic, 1, message.topic_len, stdout);
        fputs(" payload=", stdout);
        fwrite(message.payload, 1, message.payload_len, stdout);
        putchar('\n');

        ++subscriber->received;
        if (app->count && subscriber->received >= app->count)
            return mqtt_app_disconnect(app);
        return 0;
    }

    if (packet->type == 13 && !packet->flags && !packet->len &&
        subscriber->ping_pending) {
        subscriber->ping_pending = 0;
        puts("MQTT PINGRESP received");
        return 0;
    }

    fprintf(stderr, "MQTT unexpected packet type=%u state=%d\n",
            packet->type, subscriber->state);
    return -1;
}

static int subscriber_tick(mqtt_app *app, uint64_t now) {
    subscriber_context *subscriber = app->role_data;

    if (subscriber->state == WAIT_SUBACK) {
        if (now >= subscriber->subscribe_deadline) {
            fprintf(stderr, "MQTT timeout waiting in state %d\n", subscriber->state);
            return -1;
        }
        return 0;
    }

    /* CONNECT khai báo keepalive 20 giây; gửi PINGREQ sau 10 giây không gửi MQTT. */
    if (subscriber->ping_pending && now - subscriber->ping_at >= 10000) {
        fprintf(stderr, "MQTT PINGRESP timeout\n");
        return -1;
    }
    if (!subscriber->ping_pending && now - app->last_tx >= 10000) {
        mqtt_buffer buffer = {0};
        int result = mqtt_encode_pingreq(&buffer);
        if (mqtt_app_send(app, &buffer, result)) return -1;

        subscriber->ping_pending = 1;
        subscriber->ping_at = now;
        puts("MQTT PINGREQ sent");
    }
    return 0;
}

int main(int argc, char **argv) {
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
}
