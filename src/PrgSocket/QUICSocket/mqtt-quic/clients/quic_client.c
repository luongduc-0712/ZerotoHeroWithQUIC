#include "quic_client.h"

#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_gnutls.h>
#include <gnutls/crypto.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define QUEUE_CAPACITY (1024u * 1024u)
#define TLS_PRIORITY "NORMAL:-VERS-ALL:+VERS-TLS1.3:%DISABLE_TLS13_COMPAT_MODE"

struct quic_client {
    const quic_options *options;
    const quic_callbacks *callbacks;
    void *user;
    int fd;
    struct sockaddr_in local, remote;
    ngtcp2_conn *conn;
    ngtcp2_crypto_conn_ref conn_ref;
    gnutls_session_t tls;
    gnutls_certificate_credentials_t credentials;
    int64_t stream_id;
    uint8_t tx[QUEUE_CAPACITY]; /* Never realloc/memmove while ngtcp2 owns bytes. */
    uint64_t tx_queued, tx_sent, tx_acked, rx_received;
    ngtcp2_tstamp started, finish_deadline;
    int handshake, ready, finishing, tx_fin, fin_acked, rx_fin, stream_closed;
};

static volatile sig_atomic_t interrupted;

static ngtcp2_tstamp now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) {
        perror("clock_gettime");
        abort();
    }
    return (uint64_t)ts.tv_sec * NGTCP2_SECONDS + (uint64_t)ts.tv_nsec;
}

uint64_t quic_client_now_ms(void) { return now_ns() / NGTCP2_MILLISECONDS; }

static void signal_received(int signo) {
    (void)signo;
    interrupted = 1;
}

uint64_t quic_client_pending(const quic_client *c) {
    return c->tx_queued - c->tx_acked;
}

int quic_client_send(quic_client *c, const uint8_t *data, size_t length) {
    if (!c->ready || c->finishing || (!data && length) ||
        length > QUEUE_CAPACITY - quic_client_pending(c) ||
        UINT64_MAX - c->tx_queued < length) {
        fprintf(stderr, "QUIC send: stream unavailable or retransmission queue full\n");
        return -1;
    }
    size_t pos = (size_t)(c->tx_queued % QUEUE_CAPACITY);
    size_t first = length < QUEUE_CAPACITY - pos ? length : QUEUE_CAPACITY - pos;
    if (first) memcpy(c->tx + pos, data, first);
    if (length > first) memcpy(c->tx, data + first, length - first);
    c->tx_queued += length;
    return 0;
}

void quic_client_finish(quic_client *c) {
    if (!c->finishing) {
        c->finishing = 1;
        c->finish_deadline = now_ns() +
            (uint64_t)c->options->handshake_timeout_s * NGTCP2_SECONDS;
    }
}

static ngtcp2_conn *get_conn(ngtcp2_crypto_conn_ref *ref) {
    return ((quic_client *)ref->user_data)->conn;
}

static ngtcp2_path path_for(quic_client *c) {
    ngtcp2_path path = {
        .local = {(struct sockaddr *)&c->local, sizeof(c->local)},
        .remote = {(struct sockaddr *)&c->remote, sizeof(c->remote)},
        .user_data = NULL
    };
    return path;
}

static int random_cid(ngtcp2_cid *cid, size_t length) {
    uint8_t bytes[NGTCP2_MAX_CIDLEN];
    if (length > sizeof(bytes) || gnutls_rnd(GNUTLS_RND_RANDOM, bytes, length))
        return -1;
    ngtcp2_cid_init(cid, bytes, length);
    return 0;
}

static void random_bytes(uint8_t *dest, size_t length, const ngtcp2_rand_ctx *ctx) {
    (void)ctx;
    if (gnutls_rnd(GNUTLS_RND_RANDOM, dest, length)) abort();
}

static int new_cid(ngtcp2_conn *conn, ngtcp2_cid *cid,
                   ngtcp2_stateless_reset_token *token, size_t length, void *user) {
    (void)conn;
    (void)user;
    if (random_cid(cid, length) ||
        gnutls_rnd(GNUTLS_RND_RANDOM, token->data, sizeof(token->data)))
        return NGTCP2_ERR_CALLBACK_FAILURE;
    return 0;
}

static int handshake_completed(ngtcp2_conn *conn, void *user) {
    quic_client *c = user;
    gnutls_datum_t alpn;
    unsigned status = 0;
    (void)conn;
    if (gnutls_protocol_get_version(c->tls) != GNUTLS_TLS1_3 ||
        gnutls_alpn_get_selected_protocol(c->tls, &alpn) ||
        alpn.size != 4 || memcmp(alpn.data, "mqtt", 4)) {
        fprintf(stderr, "TLS verification: expected TLS1.3 and ALPN mqtt\n");
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }
    int rv = gnutls_certificate_verify_peers3(c->tls, c->options->server_name, &status);
    if (rv < 0 || status) {
        fprintf(stderr, "TLS certificate verification failed: %s (status=%u)\n",
            rv < 0 ? gnutls_strerror(rv) : "untrusted certificate or server identity", status);
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }
    c->handshake = 1;
    printf("QUIC_HANDSHAKE_OK TLS1.3 ALPN=mqtt server_name=%s certificate=verified\n",
        c->options->server_name);
    return 0;
}

static int receive_stream(ngtcp2_conn *conn, uint32_t flags, int64_t id,
                          uint64_t offset, const uint8_t *data, size_t length,
                          void *user, void *stream_user) {
    quic_client *c = user;
    (void)stream_user;
    if (!c->ready || id != c->stream_id || c->rx_fin ||
        offset != c->rx_received || UINT64_MAX - offset < length ||
        (flags & NGTCP2_STREAM_DATA_FLAG_0RTT)) {
        fprintf(stderr, "QUIC received unexpected stream, offset or early data\n");
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }
    if (length && c->callbacks->data && c->callbacks->data(c, data, length, c->user))
        return NGTCP2_ERR_CALLBACK_FAILURE;
    c->rx_received += length;
    if (length) {
        if (ngtcp2_conn_extend_max_stream_offset(conn, id, length))
            return NGTCP2_ERR_CALLBACK_FAILURE;
        ngtcp2_conn_extend_max_offset(conn, length);
    }
    if (flags & NGTCP2_STREAM_DATA_FLAG_FIN) {
        c->rx_fin = 1;
        if (!c->finishing) {
            fprintf(stderr, "QUIC broker ended the MQTT stream unexpectedly\n");
            return NGTCP2_ERR_CALLBACK_FAILURE;
        }
    }
    return 0;
}

static int acked_stream(ngtcp2_conn *conn, int64_t id, uint64_t offset,
                        uint64_t length, void *user, void *stream_user) {
    quic_client *c = user;
    (void)conn;
    (void)stream_user;
    /* ngtcp2 guarantees increasing, nonoverlapping acknowledged ranges. */
    if (id != c->stream_id || offset != c->tx_acked ||
        length > c->tx_sent - c->tx_acked) return NGTCP2_ERR_CALLBACK_FAILURE;
    c->tx_acked += length;
    /* FIN is deliberately sent alone after all application bytes are ACKed.
     * ngtcp2 reports an acknowledged empty FIN with length == 0, avoiding
     * confusion between a data ACK and an outstanding FIN-only frame. */
    if (c->tx_fin && !length && c->tx_acked == c->tx_queued) c->fin_acked = 1;
    return 0;
}

static int stream_closed(ngtcp2_conn *conn, uint32_t flags, int64_t id,
                          uint64_t rx_error, uint64_t tx_error,
                          void *user, void *stream_user) {
    quic_client *c = user;
    (void)conn;
    (void)stream_user;
    if (id != c->stream_id || flags || !c->finishing || !c->tx_fin) {
        fprintf(stderr, "QUIC stream closed unexpectedly flags=%u rx=%" PRIu64
            " tx=%" PRIu64 "\n", flags, rx_error, tx_error);
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }
    c->stream_closed = 1;
    return 0;
}

static void quic_log(void *user, char *message, size_t length) {
    (void)user;
    fputs("[ngtcp2] ", stderr);
    fwrite(message, 1, length, stderr);
    fputc('\n', stderr);
}

static int socket_open(quic_client *c) {
    char *end;
    errno = 0;
    unsigned long port = strtoul(c->options->port, &end, 10);
    c->remote.sin_family = AF_INET;
    if (errno || !c->options->port[0] || *end || !port || port > 65535 ||
        inet_pton(AF_INET, c->options->host, &c->remote.sin_addr) != 1 ||
        c->remote.sin_addr.s_addr == htonl(INADDR_ANY)) {
        fprintf(stderr, "QUIC requires a numeric IPv4 host and a port in 1..65535\n");
        return -1;
    }
    c->remote.sin_port = htons((uint16_t)port);
    c->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (c->fd < 0) { perror("QUIC UDP socket"); return -1; }
    int flags = fcntl(c->fd, F_GETFL, 0);
    if (flags < 0 || fcntl(c->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("QUIC UDP nonblocking"); return -1;
    }
    socklen_t length = sizeof(c->local);
    /* UDP connect fixes the peer and selects a concrete local source address. */
    if (connect(c->fd, (struct sockaddr *)&c->remote, sizeof(c->remote)) ||
        getsockname(c->fd, (struct sockaddr *)&c->local, &length)) {
        perror("QUIC UDP connect/getsockname"); return -1;
    }
    return 0;
}

static int connection_init(quic_client *c) {
    ngtcp2_callbacks callbacks = {
        .client_initial = ngtcp2_crypto_client_initial_cb,
        .recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb,
        .handshake_completed = handshake_completed,
        .encrypt = ngtcp2_crypto_encrypt_cb,
        .decrypt = ngtcp2_crypto_decrypt_cb,
        .hp_mask = ngtcp2_crypto_hp_mask_cb,
        .recv_stream_data = receive_stream,
        .acked_stream_data_offset = acked_stream,
        .recv_retry = ngtcp2_crypto_recv_retry_cb,
        .rand = random_bytes,
        .get_new_connection_id2 = new_cid,
        .update_key = ngtcp2_crypto_update_key_cb,
        .delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb,
        .delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb,
        .get_path_challenge_data2 = ngtcp2_crypto_get_path_challenge_data2_cb,
        .version_negotiation = ngtcp2_crypto_version_negotiation_cb,
        .stream_close2 = stream_closed
    };
    ngtcp2_settings settings;
    ngtcp2_settings_default(&settings);
    settings.initial_ts = c->started;
    settings.handshake_timeout = (uint64_t)c->options->handshake_timeout_s * NGTCP2_SECONDS;
    settings.max_tx_udp_payload_size = 1200;
    settings.no_pmtud = 1;
    if (c->options->verbose) settings.log_write = quic_log;
    ngtcp2_transport_params params;
    ngtcp2_transport_params_default(&params);
    params.initial_max_stream_data_bidi_local = QUEUE_CAPACITY;
    params.initial_max_data = QUEUE_CAPACITY;
    params.initial_max_streams_bidi = 0; /* Broker cannot open application streams. */
    params.initial_max_streams_uni = 0;
    params.max_idle_timeout = (uint64_t)c->options->idle_timeout_s * NGTCP2_SECONDS;
    params.active_connection_id_limit = 2;
    params.disable_active_migration = 1;
    ngtcp2_cid dcid, scid;
    if (random_cid(&dcid, 16) || random_cid(&scid, 16)) return -1;
    ngtcp2_path path = path_for(c);
    int rv = ngtcp2_conn_client_new(&c->conn, &dcid, &scid, &path,
        NGTCP2_PROTO_VER_V1, &callbacks, &settings, &params, NULL, c);
    if (rv) fprintf(stderr, "ngtcp2_conn_client_new: %s\n", ngtcp2_strerror(rv));
    return rv;
}

static int tls_init(quic_client *c) {
    gnutls_datum_t alpn = {(unsigned char *)"mqtt", 4};
    int rv = gnutls_certificate_allocate_credentials(&c->credentials);
    if (rv < 0) goto failed;
    rv = gnutls_certificate_set_x509_trust_file(c->credentials,
        c->options->ca_file, GNUTLS_X509_FMT_PEM);
    if (rv <= 0) {
        fprintf(stderr, "TLS CA load failed: %s: %s\n", c->options->ca_file,
            rv < 0 ? gnutls_strerror(rv) : "no certificates found");
        return -1;
    }
    rv = gnutls_init(&c->tls, GNUTLS_CLIENT | GNUTLS_NO_END_OF_EARLY_DATA);
    if (rv < 0) goto failed;
    if (ngtcp2_crypto_gnutls_configure_client_session(c->tls)) {
        fprintf(stderr, "ngtcp2 GnuTLS QUIC session configuration failed\n");
        return -1;
    }
    rv = gnutls_priority_set_direct(c->tls, TLS_PRIORITY, NULL);
    if (rv < 0) goto failed;
    rv = gnutls_credentials_set(c->tls, GNUTLS_CRD_CERTIFICATE, c->credentials);
    if (rv < 0) goto failed;
    rv = gnutls_server_name_set(c->tls, GNUTLS_NAME_DNS,
        c->options->server_name, strlen(c->options->server_name));
    if (rv < 0) goto failed;
    /* Verification is part of TLS handshake; failure aborts before MQTT CONNECT. */
    gnutls_session_set_verify_cert(c->tls, c->options->server_name, 0);
    rv = gnutls_alpn_set_protocols(c->tls, &alpn, 1, GNUTLS_ALPN_MANDATORY);
    if (rv < 0) goto failed;
    c->conn_ref.get_conn = get_conn;
    c->conn_ref.user_data = c;
    gnutls_session_set_ptr(c->tls, &c->conn_ref);
    ngtcp2_conn_set_tls_native_handle(c->conn, c->tls);
    return 0;
failed:
    fprintf(stderr, "TLS initialization failed: %s\n", gnutls_strerror(rv));
    return -1;
}

static int send_datagram(quic_client *c, const uint8_t *data, size_t length) {
    ngtcp2_tstamp deadline = now_ns() + NGTCP2_SECONDS;
    for (;;) {
        ssize_t n = send(c->fd, data, length, 0);
        if (n == (ssize_t)length) return 0;
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && now_ns() < deadline) {
            struct pollfd pfd = {.fd = c->fd, .events = POLLOUT};
            if (poll(&pfd, 1, 10) >= 0) continue;
            if (errno == EINTR) continue;
        }
        perror("QUIC send UDP");
        return -1;
    }
}

static int open_stream(quic_client *c) {
    if (!c->handshake || c->ready) return 0;
    int rv = ngtcp2_conn_open_bidi_stream(c->conn, &c->stream_id, NULL);
    if (rv == NGTCP2_ERR_STREAM_ID_BLOCKED) return 0;
    if (rv) return rv;
    c->ready = 1;
    printf("QUIC_STREAM_READY id=%" PRId64 " bidirectional=1\n", c->stream_id);
    if (c->callbacks->ready && c->callbacks->ready(c, c->user))
        return NGTCP2_ERR_CALLBACK_FAILURE;
    return 0;
}

static int flush_packets(quic_client *c) {
    uint8_t packet[1500];
    ngtcp2_tstamp now = now_ns();
    for (unsigned i = 0; i < 32; ++i) {
        ngtcp2_ssize consumed = -1;
        int64_t id = -1;
        ngtcp2_vec vectors[2];
        size_t count = 0;
        uint32_t flags = 0;
        int send_fin = c->finishing && !quic_client_pending(c);
        if (c->ready && !c->tx_fin &&
            (c->tx_sent < c->tx_queued || send_fin)) {
            id = c->stream_id;
            size_t pos = (size_t)(c->tx_sent % QUEUE_CAPACITY);
            size_t length = (size_t)(c->tx_queued - c->tx_sent);
            size_t first = length < QUEUE_CAPACITY - pos ? length : QUEUE_CAPACITY - pos;
            vectors[0] = (ngtcp2_vec){c->tx + pos, first};
            count = 1;
            if (length > first) {
                vectors[1] = (ngtcp2_vec){c->tx, length - first};
                count = 2;
            }
            if (send_fin) flags = NGTCP2_WRITE_STREAM_FLAG_FIN;
        }
        ngtcp2_path_storage path;
        ngtcp2_pkt_info info = {0};
        ngtcp2_path_storage_zero(&path);
        ngtcp2_ssize length = ngtcp2_conn_writev_stream(c->conn, &path.path, &info,
            packet, sizeof(packet), &consumed, flags, id, count ? vectors : NULL, count, now);
        if (length == NGTCP2_ERR_STREAM_DATA_BLOCKED) {
            id = -1;
            length = ngtcp2_conn_writev_stream(c->conn, &path.path, &info,
                packet, sizeof(packet), &consumed, 0, -1, NULL, 0, now);
        }
        if (length < 0) return (int)length;
        if (!length) break;
        if (consumed >= 0 && id != -1) {
            c->tx_sent += (size_t)consumed;
            if (send_fin && c->tx_sent == c->tx_queued) c->tx_fin = 1;
        }
        if (send_datagram(c, packet, (size_t)length)) return NGTCP2_ERR_CALLBACK_FAILURE;
    }
    ngtcp2_conn_update_pkt_tx_time(c->conn, now);
    return 0;
}

static size_t make_close(quic_client *c, uint8_t *packet, size_t capacity, int liberr) {
    ngtcp2_ccerr error;
    ngtcp2_ccerr_default(&error);
    if (liberr == NGTCP2_ERR_CRYPTO)
        ngtcp2_ccerr_set_tls_alert(&error, ngtcp2_conn_get_tls_alert(c->conn), NULL, 0);
    else if (liberr)
        ngtcp2_ccerr_set_liberr(&error, liberr, NULL, 0);
    else
        ngtcp2_ccerr_set_application_error(&error, 0, NULL, 0);
    ngtcp2_ssize length = ngtcp2_conn_write_connection_close(c->conn, NULL, NULL,
        packet, capacity, &error, now_ns());
    return length > 0 ? (size_t)length : 0;
}

static void close_error(quic_client *c, int liberr) {
    if (!c->conn) return;
    if (liberr < -1) {
        fprintf(stderr, "QUIC transport failure: %s\n", ngtcp2_strerror(liberr));
        if (liberr == NGTCP2_ERR_CRYPTO && c->tls) {
            unsigned status = gnutls_session_get_verify_cert_status(c->tls);
            fprintf(stderr, "TLS alert=%u certificate_status=%u\n",
                ngtcp2_conn_get_tls_alert(c->conn), status);
            if (status) fprintf(stderr, "TLS_CERTIFICATE_REJECTED status=%u\n", status);
        }
    }
    uint8_t packet[1500];
    size_t length = make_close(c, packet, sizeof(packet), liberr);
    if (length) (void)send_datagram(c, packet, length);
}

static int finish_close(quic_client *c) {
    uint8_t packet[1500], incoming[65536];
    size_t length = make_close(c, packet, sizeof(packet), 0);
    if (!length || send_datagram(c, packet, length)) return -1;
    /* Closing state lasts three PTOs. Re-send the same CLOSE if peer retries. */
    ngtcp2_duration pto = ngtcp2_conn_get_pto(c->conn);
    ngtcp2_tstamp last_sent = now_ns();
    ngtcp2_tstamp deadline = last_sent + 3 * pto;
    if (deadline > c->finish_deadline) deadline = c->finish_deadline;
    while (now_ns() < deadline) {
        if (interrupted) break;
        struct pollfd pfd = {.fd = c->fd, .events = POLLIN};
        int rv = poll(&pfd, 1, 20);
        if (rv < 0) { if (errno == EINTR) continue; return -1; }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) break;
        if (rv > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = recv(c->fd, incoming, sizeof(incoming), 0);
            if (n > 0 && now_ns() - last_sent >= pto) {
                if (send_datagram(c, packet, length)) return -1;
                last_sent = now_ns();
            }
        }
    }
    return 0;
}

static int read_packets(quic_client *c) {
    uint8_t packet[65536];
    for (unsigned i = 0; i < 64; ++i) {
        ssize_t length = recv(c->fd, packet, sizeof(packet), 0);
        if (length < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
            perror("QUIC receive UDP"); return -1;
        }
        if (!length) continue;
        ngtcp2_path path = path_for(c);
        ngtcp2_pkt_info info = {0};
        int rv = ngtcp2_conn_read_pkt(c->conn, &path, &info,
            packet, (size_t)length, now_ns());
        if (rv == NGTCP2_ERR_DRAINING) {
            const ngtcp2_ccerr *error = ngtcp2_conn_get_ccerr2(c->conn);
            if (c->finishing && c->tx_fin && !quic_client_pending(c) && !error->error_code)
                return 1;
            fprintf(stderr, "QUIC peer closed: type=%d code=%" PRIu64
                " queued=%" PRIu64 " sent=%" PRIu64 " acked=%" PRIu64 "\n",
                (int)error->type, error->error_code, c->tx_queued, c->tx_sent, c->tx_acked);
            return -1;
        }
        if (rv) return rv;
        /* This must precede processing a subsequent packet carrying stream data. */
        rv = open_stream(c);
        if (rv) return rv;
    }
    return 0;
}

static int event_loop(quic_client *c) {
    for (;;) {
        if (interrupted) {
            interrupted = 0;
            if (!c->ready || c->finishing || !c->callbacks->interrupt ||
                c->callbacks->interrupt(c, c->user)) {
                fprintf(stderr, "QUIC interrupted before graceful completion\n");
                return -1;
            }
        }
        ngtcp2_tstamp now = now_ns();
        if (!c->handshake && now - c->started >=
            (uint64_t)c->options->handshake_timeout_s * NGTCP2_SECONDS) {
            fprintf(stderr, "QUIC handshake timeout\n"); return -1;
        }
        if (c->finishing && now >= c->finish_deadline) {
            fprintf(stderr, "QUIC drain timeout pending=%" PRIu64 "\n", quic_client_pending(c));
            return -1;
        }
        if (now >= ngtcp2_conn_get_expiry(c->conn)) {
            int rv = ngtcp2_conn_handle_expiry(c->conn, now);
            if (rv) return rv;
        }
        int rv = open_stream(c);
        if (rv) return rv;
        if (!c->finishing && c->callbacks->tick &&
            c->callbacks->tick(c, quic_client_now_ms(), c->user))
            return NGTCP2_ERR_CALLBACK_FAILURE;
        rv = flush_packets(c);
        if (rv) return rv;
        if (c->finishing && c->tx_fin && c->fin_acked && !quic_client_pending(c)) {
            printf("QUIC_DRAIN_OK acked=%" PRIu64 "\n", c->tx_acked);
            return finish_close(c);
        }
        now = now_ns();
        ngtcp2_tstamp next = ngtcp2_conn_get_expiry(c->conn);
        if (c->finishing && next > c->finish_deadline) next = c->finish_deadline;
        int wait_ms = 20;
        if (next <= now) wait_ms = 0;
        else if (next - now < 20 * NGTCP2_MILLISECONDS)
            wait_ms = (int)((next - now + NGTCP2_MILLISECONDS - 1) / NGTCP2_MILLISECONDS);
        struct pollfd pfd = {.fd = c->fd, .events = POLLIN};
        rv = poll(&pfd, 1, wait_ms);
        if (rv < 0) { if (errno == EINTR) continue; perror("QUIC poll"); return -1; }
        if (pfd.revents & (POLLERR | POLLNVAL | POLLHUP)) {
            int error = 0;
            socklen_t length = sizeof(error);
            (void)getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &error, &length);
            fprintf(stderr, "QUIC UDP socket failure: %s\n",
                error ? strerror(error) : "closed socket");
            return -1;
        }
        if (pfd.revents & POLLIN) {
            rv = read_packets(c);
            if (rv == 1) return 0;
            if (rv) return rv;
        }
    }
}

int quic_client_run(const quic_options *options, const quic_callbacks *callbacks, void *user) {
    if (!options || !callbacks || !options->host || !options->port ||
        !options->ca_file || !options->server_name || !options->server_name[0] ||
        !options->handshake_timeout_s || !options->idle_timeout_s) {
        fprintf(stderr, "Invalid QUIC client options\n"); return -1;
    }
    quic_client *c = calloc(1, sizeof(*c));
    if (!c) { perror("QUIC client allocation"); return -1; }
    c->options = options;
    c->callbacks = callbacks;
    c->user = user;
    c->fd = -1;
    c->stream_id = -1;
    c->started = now_ns();
    int rv = gnutls_global_init();
    if (rv < 0) { free(c); return -1; }
    interrupted = 0;
    struct sigaction action = {0}, old_int, old_term;
    int have_int = 0, have_term = 0;
    action.sa_handler = signal_received;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, &old_int)) { perror("sigaction SIGINT"); rv = -1; goto done; }
    have_int = 1;
    if (sigaction(SIGTERM, &action, &old_term)) { perror("sigaction SIGTERM"); rv = -1; goto done; }
    have_term = 1;
    if ((rv = socket_open(c)) || (rv = connection_init(c)) || (rv = tls_init(c))) goto done;
    printf("QUIC_CONNECTING endpoint=%s:%s/udp server_name=%s\n",
        options->host, options->port, options->server_name);
    rv = event_loop(c);
    if (rv) close_error(c, rv);
done:
    /* ngtcp2 owns crypto objects referring to GnuTLS: destroy QUIC first. */
    if (c->conn) ngtcp2_conn_del(c->conn);
    if (c->tls) gnutls_deinit(c->tls);
    if (c->credentials) gnutls_certificate_free_credentials(c->credentials);
    if (c->fd != -1) close(c->fd);
    if (have_int) (void)sigaction(SIGINT, &old_int, NULL);
    if (have_term) (void)sigaction(SIGTERM, &old_term, NULL);
    gnutls_global_deinit();
    free(c);
    return rv ? -1 : 0;
}
