#ifndef MQTT_QUIC_CLIENT_H
#define MQTT_QUIC_CLIENT_H

#include <stddef.h>
#include <stdint.h>

typedef struct quic_client quic_client;

typedef struct {
    const char *host;        /* Numeric IPv4 endpoint; independent of TLS identity. */
    const char *port;
    const char *ca_file;
    const char *server_name; /* Verified DNS identity and TLS SNI, e.g. broker.test. */
    unsigned handshake_timeout_s; /* Also bounds draining during graceful shutdown. */
    unsigned idle_timeout_s; /* Advertised QUIC idle timeout, not total runtime.
                             * MQTT keepalive must produce traffic before it expires. */
    int verbose;
} quic_options;

typedef struct {
    /* Callbacks run on the event-loop thread. Return 0 or a nonzero failure. */
    int (*ready)(quic_client *, void *); /* Verified handshake and stream are ready. */
    int (*data)(quic_client *, const uint8_t *, size_t, void *); /* Ordered bytes. */
    int (*tick)(quic_client *, uint64_t now_ms, void *); /* At least every 20ms. */
    int (*interrupt)(quic_client *, void *); /* SIGINT/SIGTERM: queue DISCONNECT. */
} quic_callbacks;

/* One process, one UDP socket, one QUIC connection, one bidirectional stream.
 * The UDP socket carries ngtcp2-generated packets only. No HTTP/3 or 0-RTT. */
int quic_client_run(const quic_options *, const quic_callbacks *, void *user);

/* Copies bytes into a bounded, stable retransmission queue. Queued bytes remain
 * valid until ngtcp2 confirms their stream offsets have been acknowledged. */
int quic_client_send(quic_client *, const uint8_t *, size_t);
uint64_t quic_client_pending(const quic_client *); /* Queued minus QUIC-ACKed. */

/* Stop accepting new data, drain application bytes, send empty FIN, await ACK,
 * then send CLOSE.
 * Drain timeout and transport/TLS failures are failures of quic_client_run. */
void quic_client_finish(quic_client *);
uint64_t quic_client_now_ms(void);

#endif
