#include "tls.h"

int server_tls_credentials(tb_endpoint *ep) {
    int rv = gnutls_certificate_allocate_credentials(&ep->credentials);
    if (rv < 0) goto failed;
    rv = gnutls_certificate_set_x509_key_file(ep->credentials,
        ep->options->cert, ep->options->key, GNUTLS_X509_FMT_PEM);
    if (rv < 0) goto failed;
    return 0;
failed:
    fprintf(stderr, "Server certificate/key: %s\n", gnutls_strerror(rv));
    return -1;
}

int server_tls_session(tb_endpoint *ep) {
    int rv = gnutls_init(&ep->tls, GNUTLS_SERVER | GNUTLS_NO_AUTO_SEND_TICKET |
        GNUTLS_NO_END_OF_EARLY_DATA);
    if (rv < 0) {
        fprintf(stderr, "Server TLS: %s\n", gnutls_strerror(rv));
        return -1;
    }
    if (ngtcp2_crypto_gnutls_configure_server_session(ep->tls)) {
        fprintf(stderr, "Cannot configure GnuTLS for QUIC server.\n");
        return -1;
    }
    return tb_tls_finish(ep);
}
