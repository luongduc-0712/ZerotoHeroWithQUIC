#include "testbed.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

volatile sig_atomic_t tb_interrupted;

static void interrupted(int signo) {
    (void)signo;
    tb_interrupted = 1;
}

ngtcp2_tstamp tb_now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        abort();
    }
    return (uint64_t)ts.tv_sec * NGTCP2_SECONDS + (uint64_t)ts.tv_nsec;
}

static int number(const char *s, unsigned min, unsigned max, unsigned *out) {
    char *end;
    unsigned long value;
    if (!s[0] || s[0] < '0' || s[0] > '9') return -1;
    errno = 0;
    value = strtoul(s, &end, 10);
    if (errno || *end || value < min || value > max) return -1;
    *out = (unsigned)value;
    return 0;
}

int tb_parse_options(tb_options *o, bool server, int argc, char **argv) {
    static const struct option opts[] = {
        {"host", required_argument, NULL, 'H'},
        {"port", required_argument, NULL, 'p'},
        {"timeout", required_argument, NULL, 't'},
        {"cert", required_argument, NULL, 'c'},
        {"key", required_argument, NULL, 'k'},
        {"ca", required_argument, NULL, 'a'},
        {"server-name", required_argument, NULL, 'n'},
        {"message", required_argument, NULL, 'm'},
        {"file", required_argument, NULL, 'f'},
        {"output", required_argument, NULL, 'o'},
        {"verbose", no_argument, NULL, 'v'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    int ch;
    unsigned port;
    bool message_given = false;
    *o = (tb_options){.host = "127.0.0.1", .port = "4433",
        .cert = "certs/server.crt", .key = "certs/server.key",
        .ca = "certs/server.crt", .message = "Hello QUIC", .timeout = 15};
    while ((ch = getopt_long(argc, argv, "", opts, NULL)) != -1) {
        if ((server && strchr("anmfo", ch)) ||
            (!server && (ch == 'c' || ch == 'k'))) {
            fprintf(stderr, "Option is not supported for this endpoint.\n");
            return -1;
        }
        switch (ch) {
        case 'H': o->host = optarg; break;
        case 'p': o->port = optarg; break;
        case 't':
            if (number(optarg, 1, 3600, &o->timeout)) {
                fprintf(stderr, "--timeout must be 1..3600 seconds.\n");
                return -1;
            }
            break;
        case 'c': o->cert = optarg; break;
        case 'k': o->key = optarg; break;
        case 'a': o->ca = optarg; break;
        case 'n': o->server_name = optarg; break;
        case 'm': o->message = optarg; message_given = true; break;
        case 'f': o->input = optarg; break;
        case 'o': o->output = optarg; break;
        case 'v': o->verbose = true; break;
        case 'h':
            printf("Usage: %s [--host IPv4] [--port 4433] [--timeout 15] [--verbose]\n", argv[0]);
            puts(server ? "  [--cert certs/server.crt] [--key certs/server.key]" :
                "  [--ca certs/server.crt] [--server-name NAME]\n"
                "  [--message TEXT | --file PATH] [--output PATH]");
            puts("One connection, one bidirectional echo stream; maximum payload 65536 bytes.");
            return 1;
        default: return -1;
        }
    }
    if (optind != argc || number(o->port, 1, 65535, &port) ||
        (message_given && o->input) || (o->server_name && !o->server_name[0])) {
        fprintf(stderr, "Invalid arguments; use --help.\n");
        return -1;
    }
    if (!o->server_name) o->server_name = o->host;
    return 0;
}

static ngtcp2_conn *get_conn(ngtcp2_crypto_conn_ref *ref) {
    return ((tb_endpoint *)ref->user_data)->quic;
}

int tb_endpoint_init(tb_endpoint *ep, const tb_options *o, bool server) {
    memset(ep, 0, sizeof(*ep));
    ep->fd = -1;
    ep->stream_id = -1;
    ep->server = server;
    ep->options = o;
    ep->started = tb_now();
    ep->deadline = ep->started + (uint64_t)o->timeout * NGTCP2_SECONDS;
    ep->conn_ref.get_conn = get_conn;
    ep->conn_ref.user_data = ep;
    tb_interrupted = 0;
    struct sigaction action = {0};
    action.sa_handler = interrupted;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) {
        perror("sigaction");
        return -1;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    int rv = gnutls_global_init();
    if (rv < 0) {
        fprintf(stderr, "gnutls_global_init: %s\n", gnutls_strerror(rv));
        return -1;
    }
    return 0;
}

void tb_endpoint_free(tb_endpoint *ep) {
    /* ngtcp2 owns crypto contexts; destroy it before the TLS session. */
    if (ep->quic) ngtcp2_conn_del(ep->quic);
    if (ep->tls) gnutls_deinit(ep->tls);
    if (ep->credentials) gnutls_certificate_free_credentials(ep->credentials);
    if (ep->fd != -1) close(ep->fd);
    gnutls_global_deinit();
}

int tb_socket_open(tb_endpoint *ep) {
    unsigned port;
    struct sockaddr_in address = {.sin_family = AF_INET};
    if (number(ep->options->port, 1, 65535, &port) ||
        inet_pton(AF_INET, ep->options->host, &address.sin_addr) != 1 ||
        (!ep->server && address.sin_addr.s_addr == htonl(INADDR_ANY))) {
        fprintf(stderr, "--host must be an IPv4 address (client cannot use 0.0.0.0).\n");
        return -1;
    }
    address.sin_port = htons((uint16_t)port);
    ep->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ep->fd == -1) { perror("socket"); return -1; }
    int flags = fcntl(ep->fd, F_GETFL, 0);
    if (flags == -1 || fcntl(ep->fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl"); return -1;
    }
    if (ep->server) {
        if (bind(ep->fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
            perror("bind"); return -1;
        }
        ep->local = address;
    } else {
        ep->remote = address;
        if (tb_socket_connect(ep)) return -1;
    }
    return 0;
}

int tb_socket_connect(tb_endpoint *ep) {
    socklen_t len = sizeof(ep->local);
    /* UDP connect selects one peer and resolves the concrete local address. */
    if (connect(ep->fd, (struct sockaddr *)&ep->remote, sizeof(ep->remote)) ||
        getsockname(ep->fd, (struct sockaddr *)&ep->local, &len)) {
        perror("connect/getsockname"); return -1;
    }
    return 0;
}

ngtcp2_path tb_path(tb_endpoint *ep) {
    ngtcp2_path path = {
        .local = {(struct sockaddr *)&ep->local, sizeof(ep->local)},
        .remote = {(struct sockaddr *)&ep->remote, sizeof(ep->remote)},
        .user_data = NULL
    };
    return path;
}

int tb_random_cid(ngtcp2_cid *cid, size_t length) {
    uint8_t data[NGTCP2_MAX_CIDLEN];
    if (length > sizeof(data) || gnutls_rnd(GNUTLS_RND_RANDOM, data, length)) return -1;
    ngtcp2_cid_init(cid, data, length);
    return 0;
}

static void random_bytes(uint8_t *dest, size_t len, const ngtcp2_rand_ctx *ctx) {
    (void)ctx;
    /* This callback cannot signal failure. Never fall back to predictable bytes. */
    if (gnutls_rnd(GNUTLS_RND_RANDOM, dest, len)) abort();
}

static int new_cid(ngtcp2_conn *conn, ngtcp2_cid *cid,
                   ngtcp2_stateless_reset_token *token, size_t len, void *data) {
    (void)conn; (void)data;
    if (tb_random_cid(cid, len) ||
        gnutls_rnd(GNUTLS_RND_RANDOM, token->data, sizeof(token->data)))
        return NGTCP2_ERR_CALLBACK_FAILURE;
    return 0;
}

static int handshake_done(ngtcp2_conn *conn, void *data) {
    tb_endpoint *ep = data;
    gnutls_datum_t alpn;
    (void)conn;
    if (gnutls_alpn_get_selected_protocol(ep->tls, &alpn) ||
        alpn.size != strlen(TB_ALPN) || memcmp(alpn.data, TB_ALPN, alpn.size)) {
        fprintf(stderr, "Unexpected TLS ALPN.\n");
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }
    ep->handshake = true;
    ep->handshake_at = tb_now();
    printf("[%s] handshake complete: %s, %s, ALPN=%s\n",
        ep->server ? "server" : "client",
        gnutls_protocol_get_name(gnutls_protocol_get_version(ep->tls)),
        gnutls_cipher_get_name(gnutls_cipher_get(ep->tls)), TB_ALPN);
    return 0;
}

static int receive_stream(ngtcp2_conn *conn, uint32_t flags, int64_t id,
                          uint64_t offset, const uint8_t *data, size_t len,
                          void *user, void *stream_user) {
    tb_endpoint *ep = user;
    (void)conn; (void)stream_user;
    /* Client initiated bidirectional stream 0 is the only application stream. */
    if (id != 0 || (ep->stream_id != -1 && ep->stream_id != id) || ep->rx_fin ||
        offset != ep->rx_len || len > TB_MAX_DATA - ep->rx_len ||
        (flags & NGTCP2_STREAM_DATA_FLAG_0RTT)) {
        fprintf(stderr, "Unexpected stream, offset or payload size.\n");
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }
    ep->stream_id = id;
    if (len) memcpy(ep->rx + ep->rx_len, data, len);
    ep->rx_len += len;
    if (flags & NGTCP2_STREAM_DATA_FLAG_FIN) {
        ep->rx_fin = true;
        printf("[%s] stream=%" PRId64 " received=%zu bytes, FIN\n",
            ep->server ? "server" : "client", id, ep->rx_len);
        if (ep->server) {
            memcpy(ep->tx, ep->rx, ep->rx_len);
            ep->tx_len = ep->rx_len;
            ep->tx_ready = true;
            ep->data_started = tb_now();
        } else {
            if (ep->rx_len != ep->tx_len || memcmp(ep->rx, ep->tx, ep->rx_len)) {
                fprintf(stderr, "Echo content does not match the request.\n");
                return NGTCP2_ERR_CALLBACK_FAILURE;
            }
            ep->verified = true;
            ep->data_finished = tb_now();
            puts("[client] ECHO VERIFIED");
        }
    }
    return 0;
}

static int stream_closed(ngtcp2_conn *conn, uint32_t flags, int64_t id,
                          uint64_t rx_error, uint64_t tx_error,
                          void *user, void *stream_user) {
    tb_endpoint *ep = user;
    (void)conn; (void)stream_user; (void)rx_error; (void)tx_error;
    if (id != ep->stream_id || flags || !ep->rx_fin || !ep->tx_fin)
        return NGTCP2_ERR_CALLBACK_FAILURE;
    ep->stream_done = true;
    printf("[%s] stream closed cleanly\n", ep->server ? "server" : "client");
    return 0;
}

static void quic_log(void *user, char *message, size_t len) {
    tb_endpoint *ep = user;
    fprintf(stderr, "[%s/ngtcp2] ", ep->server ? "server" : "client");
    fwrite(message, 1, len, stderr);
    fputc('\n', stderr);
}

void tb_quic_config(tb_endpoint *ep, ngtcp2_callbacks *cb,
                    ngtcp2_settings *settings, ngtcp2_transport_params *params) {
    *cb = (ngtcp2_callbacks){
        .client_initial = ep->server ? NULL : ngtcp2_crypto_client_initial_cb,
        .recv_client_initial = ep->server ? ngtcp2_crypto_recv_client_initial_cb : NULL,
        .recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb,
        .handshake_completed = handshake_done,
        .encrypt = ngtcp2_crypto_encrypt_cb,
        .decrypt = ngtcp2_crypto_decrypt_cb,
        .hp_mask = ngtcp2_crypto_hp_mask_cb,
        .recv_stream_data = receive_stream,
        .recv_retry = ep->server ? NULL : ngtcp2_crypto_recv_retry_cb,
        .rand = random_bytes,
        .get_new_connection_id2 = new_cid,
        .update_key = ngtcp2_crypto_update_key_cb,
        .delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb,
        .delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb,
        .get_path_challenge_data2 = ngtcp2_crypto_get_path_challenge_data2_cb,
        .version_negotiation = ngtcp2_crypto_version_negotiation_cb,
        .stream_close2 = stream_closed
    };
    ngtcp2_settings_default(settings);
    settings->initial_ts = tb_now();
    settings->handshake_timeout = (uint64_t)ep->options->timeout * NGTCP2_SECONDS;
    settings->max_tx_udp_payload_size = 1200;
    settings->no_pmtud = 1;
    settings->cc_algo = NGTCP2_CC_ALGO_CUBIC;
    if (ep->options->verbose) settings->log_write = quic_log;
    ngtcp2_transport_params_default(params);
    params->initial_max_stream_data_bidi_local = TB_MAX_DATA;
    params->initial_max_stream_data_bidi_remote = TB_MAX_DATA;
    params->initial_max_data = 2 * TB_MAX_DATA;
    params->initial_max_streams_bidi = ep->server ? 1 : 0;
    params->initial_max_streams_uni = 0;
    params->max_idle_timeout = (uint64_t)ep->options->timeout * NGTCP2_SECONDS;
    params->active_connection_id_limit = 2;
    params->disable_active_migration = 1;
}

int tb_tls_finish(tb_endpoint *ep) {
    gnutls_datum_t alpn = {(unsigned char *)TB_ALPN, sizeof(TB_ALPN) - 1};
    int rv = gnutls_priority_set_direct(ep->tls, TB_TLS_PRIORITY, NULL);
    if (rv < 0) goto failed;
    rv = gnutls_credentials_set(ep->tls, GNUTLS_CRD_CERTIFICATE, ep->credentials);
    if (rv < 0) goto failed;
    rv = gnutls_alpn_set_protocols(ep->tls, &alpn, 1, GNUTLS_ALPN_MANDATORY |
        (ep->server ? GNUTLS_ALPN_SERVER_PRECEDENCE : 0));
    if (rv < 0) goto failed;
    gnutls_session_set_ptr(ep->tls, &ep->conn_ref);
    ngtcp2_conn_set_tls_native_handle(ep->quic, ep->tls);
    return 0;
failed:
    fprintf(stderr, "TLS setup: %s\n", gnutls_strerror(rv));
    return -1;
}

static int send_datagram(tb_endpoint *ep, const uint8_t *data, size_t len) {
    for (;;) {
        ssize_t n = send(ep->fd, data, len, 0);
        if (n == (ssize_t)len) {
            ++ep->udp_sent;
            ep->wire_sent += len;
            return 0;
        }
        if (n < 0 && errno == EINTR && !tb_interrupted) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) &&
            !tb_interrupted && tb_now() < ep->deadline) {
            struct pollfd pfd = {.fd = ep->fd, .events = POLLOUT};
            if (poll(&pfd, 1, 10) >= 0) continue;
        }
        perror("send UDP");
        return -1;
    }
}

static int flush_packets(tb_endpoint *ep) {
    uint8_t packet[1500];
    ngtcp2_tstamp now = tb_now();
    for (unsigned i = 0; i < 32; ++i) {
        ngtcp2_ssize consumed = -1;
        int64_t stream = -1;
        ngtcp2_vec vec;
        size_t veccnt = 0;
        uint32_t flags = 0;
        if (!ep->server && ep->handshake && ep->stream_id == -1) {
            int rv = ngtcp2_conn_open_bidi_stream(ep->quic, &ep->stream_id, NULL);
            if (rv && rv != NGTCP2_ERR_STREAM_ID_BLOCKED) return rv;
            if (!rv) { ep->tx_ready = true; ep->data_started = now; }
        }
        if (ep->tx_ready && !ep->tx_fin) {
            stream = ep->stream_id;
            vec.base = ep->tx + ep->tx_offset;
            vec.len = ep->tx_len - ep->tx_offset;
            veccnt = 1;
            flags = NGTCP2_WRITE_STREAM_FLAG_FIN;
        }
        ngtcp2_path_storage ps;
        ngtcp2_pkt_info pi = {0};
        ngtcp2_path_storage_zero(&ps);
        ngtcp2_ssize n = ngtcp2_conn_writev_stream(ep->quic, &ps.path, &pi,
            packet, sizeof(packet), &consumed, flags, stream,
            veccnt ? &vec : NULL, veccnt, now);
        if (n < 0) return (int)n;
        if (!n) break;
        if (consumed >= 0 && stream != -1) {
            ep->tx_offset += (size_t)consumed;
            if (ep->tx_offset == ep->tx_len) ep->tx_fin = true;
        }
        /* tx[] remains allocated until ngtcp2_conn_del, including retransmits. */
        if (send_datagram(ep, packet, (size_t)n)) return NGTCP2_ERR_CALLBACK_FAILURE;
    }
    ngtcp2_conn_update_pkt_tx_time(ep->quic, now);
    return 0;
}

static size_t make_close(tb_endpoint *ep, uint8_t *packet, size_t capacity,
                          int liberr) {
    ngtcp2_ccerr error;
    ngtcp2_ccerr_default(&error);
    if (liberr == NGTCP2_ERR_CRYPTO)
        ngtcp2_ccerr_set_tls_alert(&error, ngtcp2_conn_get_tls_alert(ep->quic), NULL, 0);
    else if (liberr)
        ngtcp2_ccerr_set_liberr(&error, liberr, NULL, 0);
    else
        ngtcp2_ccerr_set_application_error(&error, 0, NULL, 0);
    ngtcp2_ssize n = ngtcp2_conn_write_connection_close(ep->quic, NULL, NULL,
        packet, capacity, &error, tb_now());
    return n > 0 ? (size_t)n : 0;
}

void tb_send_error(tb_endpoint *ep, int liberr) {
    if (!ep->quic) return;
    uint8_t packet[1500];
    size_t len = make_close(ep, packet, sizeof(packet), liberr);
    if (len) (void)send_datagram(ep, packet, len);
}

int tb_read_packet(tb_endpoint *ep, const uint8_t *data, size_t length) {
    ngtcp2_path path = tb_path(ep);
    ngtcp2_pkt_info pi = {0};
    ++ep->udp_received;
    ep->wire_received += length;
    return ngtcp2_conn_read_pkt(ep->quic, &path, &pi, data, length, tb_now());
}

static int finish_connection(tb_endpoint *ep) {
    uint8_t packet[1500], incoming[65536];
    size_t len = make_close(ep, packet, sizeof(packet), 0);
    if (!len || send_datagram(ep, packet, len)) return -1;
    /* Keep the closing period for 3 PTOs; repeat CLOSE if peer retransmits. */
    ngtcp2_duration pto = ngtcp2_conn_get_pto(ep->quic);
    ngtcp2_tstamp last_sent = tb_now();
    ngtcp2_tstamp stop = last_sent + 3 * pto;
    if (stop > ep->deadline) stop = ep->deadline;
    while (!tb_interrupted && tb_now() < stop) {
        struct pollfd pfd = {.fd = ep->fd, .events = POLLIN};
        int rv = poll(&pfd, 1, 20);
        if (rv < 0) { if (errno == EINTR) continue; return -1; }
        if (rv > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = recv(ep->fd, incoming, sizeof(incoming), 0);
            if (n > 0) {
                ++ep->udp_received;
                ep->wire_received += (size_t)n;
                if (tb_now() - last_sent >= pto) {
                    if (send_datagram(ep, packet, len)) return -1;
                    last_sent = tb_now();
                }
            } else if (n < 0 && errno == ECONNREFUSED) break;
        }
        if (pfd.revents & POLLERR) break;
    }
    return tb_interrupted ? -1 : 0;
}

int tb_event_loop(tb_endpoint *ep) {
    uint8_t packet[65536];
    while (!tb_interrupted) {
        ngtcp2_tstamp now = tb_now();
        if (now >= ep->deadline) { fprintf(stderr, "Test deadline expired.\n"); return -1; }
        if (now >= ngtcp2_conn_get_expiry(ep->quic)) {
            int rv = ngtcp2_conn_handle_expiry(ep->quic, now);
            if (rv) { tb_send_error(ep, rv); return rv; }
        }
        int rv = flush_packets(ep);
        if (rv) { tb_send_error(ep, rv); return rv; }
        if (ep->stream_done && (ep->server || ep->verified)) return finish_connection(ep);
        now = tb_now();
        ngtcp2_tstamp next = ngtcp2_conn_get_expiry(ep->quic);
        if (next > ep->deadline) next = ep->deadline;
        uint64_t delay_ms = next > now ? (next - now + NGTCP2_MILLISECONDS - 1) /
            NGTCP2_MILLISECONDS : 0;
        int wait_ms = delay_ms > 20 ? 20 : (int)delay_ms;
        struct pollfd pfd = {.fd = ep->fd, .events = POLLIN};
        rv = poll(&pfd, 1, wait_ms);
        if (rv < 0) { if (errno == EINTR) continue; perror("poll"); return -1; }
        if (!rv) continue;
        if (pfd.revents & (POLLERR | POLLNVAL | POLLHUP)) {
            int error = 0;
            socklen_t len = sizeof(error);
            (void)getsockopt(ep->fd, SOL_SOCKET, SO_ERROR, &error, &len);
            fprintf(stderr, "UDP socket error: %s\n", error ? strerror(error) : "socket closed");
            return -1;
        }
        if (!(pfd.revents & POLLIN)) continue;
        /* Bound each receive batch so timers cannot be starved by a busy peer. */
        for (unsigned i = 0; i < 64; ++i) {
            ssize_t n = recv(ep->fd, packet, sizeof(packet), 0);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break;
                perror("recv UDP"); return -1;
            }
            if (!n) continue;
            rv = tb_read_packet(ep, packet, (size_t)n);
            if (rv == NGTCP2_ERR_DRAINING) {
                const ngtcp2_ccerr *error = ngtcp2_conn_get_ccerr2(ep->quic);
                /* A clean application CLOSE can arrive before the delayed ACK
                 * for our FIN. Client sends code 0 only after verifying its echo. */
                if (ep->server && ep->handshake && ep->rx_fin && ep->tx_fin &&
                    ep->tx_offset == ep->tx_len && error->error_code == 0 &&
                    error->type == NGTCP2_CCERR_TYPE_APPLICATION) {
                    puts("[server] peer confirmed completion with application CLOSE(0)");
                    return 0;
                }
                fprintf(stderr, "Peer closed before successful completion (code=%" PRIu64 ").\n",
                    error->error_code);
                return -1;
            }
            if (rv) { tb_send_error(ep, rv); return rv; }
        }
    }
    fprintf(stderr, "Interrupted.\n");
    return -1;
}

void tb_print_stats(tb_endpoint *ep, int result) {
    printf("[%s] result=%s app_tx=%zu app_rx=%zu udp_tx=%" PRIu64
           " udp_rx=%" PRIu64 " wire_tx=%" PRIu64 " wire_rx=%" PRIu64 "\n",
        ep->server ? "server" : "client", result ? "FAIL" : "PASS",
        ep->tx_offset, ep->rx_len, ep->udp_sent, ep->udp_received,
        ep->wire_sent, ep->wire_received);
    if (ep->quic) {
        ngtcp2_conn_info info;
        ngtcp2_conn_get_conn_info2(ep->quic, &info);
        printf("[%s] handshake_ms=%.3f srtt_ms=%.3f cwnd=%" PRIu64
               " lost_packets=%" PRIu64 "\n", ep->server ? "server" : "client",
            ep->handshake ? (double)(ep->handshake_at - ep->started) / 1e6 : -1.0,
            (double)info.smoothed_rtt / 1e6, info.cwnd, info.pkt_lost);
    }
    if (!ep->server && ep->verified)
        printf("[client] echo_ms=%.3f\n", (double)(ep->data_finished - ep->data_started) / 1e6);
    if (result < -1) fprintf(stderr, "ngtcp2: %s\n", ngtcp2_strerror(result));
}

int tb_load_payload(tb_endpoint *ep) {
    if (!ep->options->input) {
        ep->tx_len = strlen(ep->options->message);
        if (ep->tx_len > TB_MAX_DATA) { fprintf(stderr, "Message exceeds 65536 bytes.\n"); return -1; }
        memcpy(ep->tx, ep->options->message, ep->tx_len);
        return 0;
    }
    FILE *f = fopen(ep->options->input, "rb");
    if (!f) { perror("open input"); return -1; }
    ep->tx_len = fread(ep->tx, 1, sizeof(ep->tx), f);
    int extra = fgetc(f);
    int bad = ferror(f) || extra != EOF;
    if (fclose(f)) bad = 1;
    if (bad) fprintf(stderr, "Cannot read input or input exceeds 65536 bytes.\n");
    return bad ? -1 : 0;
}

int tb_save_echo(tb_endpoint *ep) {
    if (!ep->options->output) return 0;
    FILE *f = fopen(ep->options->output, "wb");
    if (!f) { perror("open output"); return -1; }
    int bad = fwrite(ep->rx, 1, ep->rx_len, f) != ep->rx_len;
    if (fclose(f)) bad = 1;
    if (bad) perror("write output");
    return bad ? -1 : 0;
}
