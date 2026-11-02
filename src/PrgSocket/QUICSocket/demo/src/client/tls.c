#include "tls.h"
#include <arpa/inet.h>
#include <string.h>

int client_tls_init(tb_endpoint *ep) {
    int rv = gnutls_certificate_allocate_credentials(&ep->credentials);
    if (rv < 0) goto failed;
    rv = gnutls_certificate_set_x509_trust_file(ep->credentials,
        ep->options->ca, GNUTLS_X509_FMT_PEM);
    if (rv <= 0) {
        fprintf(stderr, "Cannot load CA certificate %s: %s\n", ep->options->ca,
            rv < 0 ? gnutls_strerror(rv) : "no certificates found");
        return -1;
    }
    rv = gnutls_init(&ep->tls, GNUTLS_CLIENT | GNUTLS_NO_END_OF_EARLY_DATA);
    if (rv < 0) goto failed;
    if (ngtcp2_crypto_gnutls_configure_client_session(ep->tls)) {
        fprintf(stderr, "Cannot configure GnuTLS for QUIC client.\n");
        return -1;
    }
    struct in_addr numeric;
    if (inet_pton(AF_INET, ep->options->server_name, &numeric) != 1) {
        rv = gnutls_server_name_set(ep->tls, GNUTLS_NAME_DNS,
            ep->options->server_name, strlen(ep->options->server_name));
        if (rv < 0) goto failed;
    }
    /* GnuTLS checks trust, validity and DNS/IP identity during the handshake. */
    gnutls_session_set_verify_cert(ep->tls, ep->options->server_name, 0);
    return tb_tls_finish(ep);
failed:
    fprintf(stderr, "Client TLS: %s\n", gnutls_strerror(rv));
    return -1;
}
