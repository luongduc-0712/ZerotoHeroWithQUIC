#include "server.h"

int main(int argc, char **argv) {
    tb_options options;
    int rv = tb_parse_options(&options, true, argc, argv);
    if (rv) return rv > 0 ? 0 : 2;
    return server_run(&options) ? 1 : 0;
}
