#ifndef MQTT_CODEC_H
#define MQTT_CODEC_H
#include <stddef.h>
#include <stdint.h>

/* Bound memory use in this minimal lab client, not a protocol-wide limit. */
#define MQTT_MAX_PACKET_SIZE (1024u * 1024u)
typedef struct { uint8_t *data; size_t len; } mqtt_buffer;
typedef struct {
    uint8_t type, flags;
    const uint8_t *body;
    size_t len;
} mqtt_packet;
typedef int (*mqtt_packet_cb)(const mqtt_packet *, void *);
typedef struct { uint8_t *data; size_t len, cap; } mqtt_parser;
typedef struct {
    const uint8_t *topic, *payload;
    size_t topic_len, payload_len;
    int retained;
} mqtt_publish;

void mqtt_buffer_free(mqtt_buffer *);
void mqtt_parser_free(mqtt_parser *);
/* May deliver zero, one, or many packets. Callback views expire on return. */
int mqtt_parser_feed(mqtt_parser *, const uint8_t *, size_t, mqtt_packet_cb, void *);
int mqtt_encode_connect(mqtt_buffer *, const char *client_id, uint16_t keepalive);
int mqtt_encode_subscribe(mqtt_buffer *, uint16_t packet_id, const char *topic);
int mqtt_encode_publish(mqtt_buffer *, const char *topic, const uint8_t *payload, size_t len);
int mqtt_encode_pingreq(mqtt_buffer *);
int mqtt_encode_disconnect(mqtt_buffer *);
int mqtt_decode_connack(const mqtt_packet *, uint8_t *session_present, uint8_t *return_code);
int mqtt_decode_suback(const mqtt_packet *, uint16_t *packet_id, uint8_t *granted_qos);
int mqtt_decode_publish(const mqtt_packet *, mqtt_publish *);
#endif
