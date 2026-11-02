#include "connection.h"

int client_connection_new(tb_endpoint *ep) {
    ngtcp2_callbacks callbacks;
    ngtcp2_settings settings;
    ngtcp2_transport_params params;
    ngtcp2_cid dcid, scid;
    tb_quic_config(ep, &callbacks, &settings, &params);
    if (tb_random_cid(&dcid, 16) || tb_random_cid(&scid, 16)) return -1;
    ngtcp2_path path = tb_path(ep);
    return ngtcp2_conn_client_new(&ep->quic, &dcid, &scid, &path,
        NGTCP2_PROTO_VER_V1, &callbacks, &settings, &params, NULL, ep);
}
