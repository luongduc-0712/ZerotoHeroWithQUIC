#ifndef QUIC_TESTBED_H
#define QUIC_TESTBED_H

#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_gnutls.h>
#include <gnutls/crypto.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define TB_MAX_DATA 65536u
#define TB_ALPN "quic-testbed/1"
#define TB_TLS_PRIORITY "NORMAL:-VERS-ALL:+VERS-TLS1.3:%DISABLE_TLS13_COMPAT_MODE"

typedef struct {
    const char *host, *port, *cert, *key, *ca, *server_name;
    const char *message, *input, *output;
    unsigned timeout;
    bool verbose;
} tb_options;

typedef struct {
    bool server;
    int fd;
    struct sockaddr_in local, remote;
    ngtcp2_conn *quic;
    ngtcp2_crypto_conn_ref conn_ref;
    gnutls_session_t tls;
    gnutls_certificate_credentials_t credentials;
    const tb_options *options;
    int64_t stream_id;
    uint8_t tx[TB_MAX_DATA], rx[TB_MAX_DATA];
    size_t tx_len, tx_offset, rx_len;
    bool handshake, tx_ready, tx_fin, rx_fin, stream_done, verified;
    ngtcp2_tstamp started, deadline, handshake_at, data_started, data_finished;
    uint64_t udp_sent, udp_received, wire_sent, wire_received;
} tb_endpoint;

extern volatile sig_atomic_t tb_interrupted;
ngtcp2_tstamp tb_now(void);
int tb_parse_options(tb_options *o, bool server, int argc, char **argv);
int tb_endpoint_init(tb_endpoint *ep, const tb_options *o, bool server);
void tb_endpoint_free(tb_endpoint *ep);
int tb_socket_open(tb_endpoint *ep);
int tb_socket_connect(tb_endpoint *ep);
ngtcp2_path tb_path(tb_endpoint *ep);
void tb_quic_config(tb_endpoint *ep, ngtcp2_callbacks *cb,
                    ngtcp2_settings *settings, ngtcp2_transport_params *params);
int tb_random_cid(ngtcp2_cid *cid, size_t length);
int tb_tls_finish(tb_endpoint *ep);
int tb_read_packet(tb_endpoint *ep, const uint8_t *data, size_t length);
int tb_event_loop(tb_endpoint *ep);
void tb_send_error(tb_endpoint *ep, int liberr);
void tb_print_stats(tb_endpoint *ep, int result);
int tb_load_payload(tb_endpoint *ep);
int tb_save_echo(tb_endpoint *ep);

#endif
