#include "client.h"

int main(int argc, char **argv) {
    tb_options options;
    int rv = tb_parse_options(&options, false, argc, argv);
    if (rv) return rv > 0 ? 0 : 2;
    return client_run(&options) ? 1 : 0;
}
