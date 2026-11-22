#include "app.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t publish_deadline;
} publisher;

/* Session chung chỉ gọi hàm này sau CONNACK thành công. */
static int publisher_connected(mqtt_app *app) {
    publisher *pub = app->role_data;
    mqtt_buffer buffer = {0};
    int rc = mqtt_encode_publish(&buffer, app->topic,
                                (const uint8_t *)app->message, strlen(app->message));
    if (mqtt_app_send(app, &buffer, rc)) return -1;
    pub->publish_deadline = quic_client_now_ms() + 15000;
    printf("MQTT PUBLISH QoS 0 sent topic=%s\n", app->topic);
    return 0;
}

static int publisher_packet(mqtt_app *app, const mqtt_packet *packet) {
    (void)app;
    /* QoS 0 không có MQTT PUBACK; completion được kiểm tra bằng QUIC ACK. */
    fprintf(stderr, "MQTT publisher received unexpected packet type=%u\n", packet->type);
    return -1;
}

static int publisher_tick(mqtt_app *app, uint64_t now) {
    publisher *pub = app->role_data;
    if (now >= pub->publish_deadline) {
        fprintf(stderr, "MQTT timeout waiting for PUBLISH QUIC acknowledgement\n");
        return -1;
    }
    if (!quic_client_pending(app->quic)) {
        puts("MQTT PUBLISH acknowledged by QUIC (QoS 0 has no MQTT PUBACK)");
        return mqtt_app_disconnect(app);
    }
    return 0;
}

int main(int argc, char **argv) {
    publisher pub = {0};
    const mqtt_role role = {
        .name = "publisher", .subscriber = 0, .default_timeout = 30,
        .connected = publisher_connected, .packet = publisher_packet, .tick = publisher_tick
    };
    return mqtt_app_main(argc, argv, &role, &pub);
}
