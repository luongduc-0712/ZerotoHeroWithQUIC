#include "server.h"
#include "connection.h"
#include "tls.h"

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>

static int accept_initial(tb_endpoint *ep, uint8_t *packet, size_t capacity,
                           size_t *length, ngtcp2_pkt_hd *hd) {
    while (!tb_interrupted && tb_now() < ep->deadline) {
        struct pollfd pfd = {.fd = ep->fd, .events = POLLIN};
        int rv = poll(&pfd, 1, 100);
        if (rv < 0) { if (errno == EINTR) continue; perror("poll initial"); return -1; }
        if (!rv) continue;
        if (pfd.revents & (POLLERR | POLLNVAL | POLLHUP)) return -1;
        if (!(pfd.revents & POLLIN)) continue;
        socklen_t len = sizeof(ep->remote);
        ssize_t n = recvfrom(ep->fd, packet, capacity, 0,
            (struct sockaddr *)&ep->remote, &len);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            perror("recvfrom initial"); return -1;
        }
        if (n < 1200 || ngtcp2_accept(hd, packet, (size_t)n) ||
            hd->version != NGTCP2_PROTO_VER_V1) continue;
        *length = (size_t)n;
        return tb_socket_connect(ep);
    }
    fprintf(stderr, "No valid QUIC v1 Initial before timeout, or interrupted.\n");
    return -1;
}

int server_run(const tb_options *options) {
    tb_endpoint ep;
    int rv = tb_endpoint_init(&ep, options, true);
    if (rv) return rv;
    if ((rv = server_tls_credentials(&ep)) || (rv = tb_socket_open(&ep))) goto done;
    printf("[server] listening on %s:%s UDP, waiting for ONE client\n",
        options->host, options->port);
    uint8_t initial[65536];
    size_t length;
    ngtcp2_pkt_hd hd;
    if ((rv = accept_initial(&ep, initial, sizeof(initial), &length, &hd))) goto done;
    char peer[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ep.remote.sin_addr, peer, sizeof(peer));
    printf("[server] accepted %s:%u, QUIC v1\n", peer, ntohs(ep.remote.sin_port));
    /* Report handshake duration from the first accepted Initial, excluding wait. */
    ep.started = tb_now();
    ep.deadline = ep.started + (uint64_t)options->timeout * NGTCP2_SECONDS;
    if ((rv = server_connection_new(&ep, &hd)) || (rv = server_tls_session(&ep))) goto done;
    if ((rv = tb_read_packet(&ep, initial, length))) {
        tb_send_error(&ep, rv);
        goto done;
    }
    rv = tb_event_loop(&ep);
done:
    tb_print_stats(&ep, rv);
    tb_endpoint_free(&ep);
    return rv;
}
