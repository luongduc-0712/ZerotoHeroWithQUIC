#ifndef TESTBED_SERVER_CONNECTION_H
#define TESTBED_SERVER_CONNECTION_H
#include "testbed.h"
int server_connection_new(tb_endpoint *ep, const ngtcp2_pkt_hd *initial);
#endif
