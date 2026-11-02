#include "connection.h"

int server_connection_new(tb_endpoint *ep, const ngtcp2_pkt_hd *initial) {
    ngtcp2_callbacks callbacks;
    ngtcp2_settings settings;
    ngtcp2_transport_params params;
    ngtcp2_cid scid;
    tb_quic_config(ep, &callbacks, &settings, &params);
    params.original_dcid = initial->dcid;
    params.original_dcid_present = 1;
    if (tb_random_cid(&scid, 16)) return -1;
    ngtcp2_path path = tb_path(ep);
    /* Destination is the client's SCID; original DCID is a transport parameter. */
    return ngtcp2_conn_server_new(&ep->quic, &initial->scid, &scid, &path,
        initial->version, &callbacks, &settings, &params, NULL, ep);
}
