#include "app.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Session chung: CLI, CONNECT/CONNACK, ghép byte stream và shutdown.
 * Logic publisher/subscriber được định nghĩa trong hai source riêng. */
int mqtt_app_send(mqtt_app *app, mqtt_buffer *buffer, int encoding_result) {
    if (encoding_result || quic_client_send(app->quic, buffer->data, buffer->len)) {
        mqtt_buffer_free(buffer);
        fprintf(stderr, "MQTT: cannot encode/queue packet\n");
        return -1;
    }
    mqtt_buffer_free(buffer);
    app->last_tx = quic_client_now_ms();
    return 0;
}

int mqtt_app_disconnect(mqtt_app *app) {
    if (app->stopping) return 0;
    if (app->connected) {
        mqtt_buffer buffer = {0};
        int rc = mqtt_encode_disconnect(&buffer);
        if (mqtt_app_send(app, &buffer, rc)) return -1;
        puts("MQTT DISCONNECT queued");
    }
    app->stopping = 1;
    quic_client_finish(app->quic);
    return 0;
}

static int on_ready(quic_client *client, void *user) {
    mqtt_app *app = user;
    app->quic = client;
    mqtt_buffer buffer = {0};
    int rc = mqtt_encode_connect(&buffer, app->client_id, 20);
    if (mqtt_app_send(app, &buffer, rc)) return -1;
    app->connect_sent = 1;
    app->deadline = quic_client_now_ms() + 15000;
    puts("MQTT CONNECT sent (3.1.1, clean session, keepalive=20s)");
    return 0;
}

static int on_packet(const mqtt_packet *packet, void *user) {
    mqtt_app *app = user;
    if (!app->connected) {
        uint8_t session_present, return_code;
        if (!app->connect_sent ||
            mqtt_decode_connack(packet, &session_present, &return_code) ||
            session_present || return_code) {
            fprintf(stderr, "MQTT CONNACK invalid or rejected\n");
            return -1;
        }
        app->connected = 1;
        puts("MQTT CONNACK accepted");
        return app->role->connected(app);
    }
    return app->role->packet(app, packet);
}

static int on_data(quic_client *client, const uint8_t *data, size_t length, void *user) {
    mqtt_app *app = user;
    (void)client;
    if (mqtt_parser_feed(&app->parser, data, length, on_packet, app)) {
        fprintf(stderr, "MQTT stream decode/handler failed\n");
        return -1;
    }
    return 0;
}

static int on_tick(quic_client *client, uint64_t now, void *user) {
    mqtt_app *app = user;
    app->quic = client;
    if (app->stopping) return 0;
    if (app->timeout && now - app->started >= (uint64_t)app->timeout * 1000) {
        fprintf(stderr, "MQTT application timeout\n");
        return -1;
    }
    if (app->connect_sent && !app->connected && now >= app->deadline) {
        fprintf(stderr, "MQTT CONNACK timeout\n");
        return -1;
    }
    return app->connected ? app->role->tick(app, now) : 0;
}

static int on_interrupt(quic_client *client, void *user) {
    mqtt_app *app = user;
    app->quic = client;
    puts("Stopping on signal");
    return mqtt_app_disconnect(app);
}

static int unsigned_arg(const char *text, unsigned *out) {
    char *end;
    errno = 0;
    if (!*text || *text == '-') return -1;
    unsigned long n = strtoul(text, &end, 10);
    if (errno || *end || n > UINT_MAX) return -1;
    *out = (unsigned)n;
    return 0;
}

static void usage(const char *name, const mqtt_role *role) {
    printf("Usage: %s [--host IP] [--port 14567] [--ca-file FILE]\n"
           "  [--server-name broker.test] [--topic lab/temperature] [--client-id ID]\n"
           "  [--timeout SECONDS] [--verbose] %s\n", name,
           role->subscriber ? "[--count N (0=unlimited)]" : "[--message 'Temperature = 28.5 C']");
}

int mqtt_app_main(int argc, char **argv, const mqtt_role *role, void *role_data) {
    quic_options options = {
        .host = "10.77.0.254", .port = "14567", .ca_file = "docker/certs/ca.pem",
        .server_name = "broker.test", .handshake_timeout_s = 15, .idle_timeout_s = 60
    };
    char client_id[64];
    snprintf(client_id, sizeof(client_id), "mqtt-%s-%ld", role->name, (long)getpid());
    mqtt_app app = {
        .topic = "lab/temperature", .message = "Temperature = 28.5 C", .client_id = client_id,
        .timeout = role->default_timeout, .role = role, .role_data = role_data
    };
    static const struct option cli_options[] = {
        {"host", required_argument, NULL, 'H'}, {"port", required_argument, NULL, 'p'},
        {"ca-file", required_argument, NULL, 'a'}, {"server-name", required_argument, NULL, 's'},
        {"topic", required_argument, NULL, 't'}, {"client-id", required_argument, NULL, 'i'},
        {"message", required_argument, NULL, 'm'}, {"count", required_argument, NULL, 'c'},
        {"timeout", required_argument, NULL, 'T'}, {"verbose", no_argument, NULL, 'v'},
        {"help", no_argument, NULL, 'h'}, {NULL, 0, NULL, 0}
    };
    for (int option; (option = getopt_long(argc, argv, "", cli_options, NULL)) != -1;) {
        switch (option) {
        case 'H': options.host = optarg; break;
        case 'p': {
            unsigned port;
            if (unsigned_arg(optarg, &port) || !port || port > 65535) goto invalid;
            options.port = optarg;
            break;
        }
        case 'a': options.ca_file = optarg; break;
        case 's': options.server_name = optarg; break;
        case 't': app.topic = optarg; break;
        case 'i': app.client_id = optarg; break;
        case 'm': if (role->subscriber) goto invalid; app.message = optarg; break;
        case 'c': if (!role->subscriber || unsigned_arg(optarg, &app.count)) goto invalid; break;
        case 'T': if (unsigned_arg(optarg, &app.timeout)) goto invalid; break;
        case 'v': options.verbose = 1; break;
        case 'h': usage(argv[0], role); return 0;
        default: goto invalid;
        }
    }
    if (optind != argc) goto invalid;
    setvbuf(stdout, NULL, _IOLBF, 0);
    app.started = quic_client_now_ms();
    const quic_callbacks callbacks = {
        .ready = on_ready, .data = on_data, .tick = on_tick, .interrupt = on_interrupt
    };
    int rc = quic_client_run(&options, &callbacks, &app);
    if (!rc && app.parser.len) {
        fprintf(stderr, "MQTT connection ended with truncated packet\n");
        rc = -1;
    }
    mqtt_parser_free(&app.parser);
    return rc ? 1 : 0;
invalid:
    usage(argv[0], role);
    return 2;
}
