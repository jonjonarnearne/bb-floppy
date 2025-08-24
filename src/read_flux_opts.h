#ifndef READ_FLUX_OPTS_H
#define READ_FLUX_OPTS_H

#include <stdbool.h>

struct read_flux_opts {
        const char *filename;
};

void read_flux_opts_print_usage(char * const argv[]);
bool read_flux_opts_parse(struct read_flux_opts *opts, int argc, char * const argv[]);

#endif // READ_FLUX_OPTS_H

