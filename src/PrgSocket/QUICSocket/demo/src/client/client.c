#include "client.h"
#include "connection.h"
#include "tls.h"

int client_run(const tb_options *options) {
    tb_endpoint ep;
    int rv = tb_endpoint_init(&ep, options, false);
    if (rv) return rv;
    if ((rv = tb_load_payload(&ep)) || (rv = tb_socket_open(&ep)) ||
        (rv = client_connection_new(&ep)) || (rv = client_tls_init(&ep))) goto done;
    printf("[client] connecting to %s:%s UDP, payload=%zu bytes\n",
        options->host, options->port, ep.tx_len);
    rv = tb_event_loop(&ep);
    if (!rv) rv = tb_save_echo(&ep);
done:
    tb_print_stats(&ep, rv);
    tb_endpoint_free(&ep);
    return rv;
}
