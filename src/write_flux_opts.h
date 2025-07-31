#ifndef WRITE_FLUX_OPTS_H
#define WRITE_FLUX_OPTS_H

#include <stdbool.h>

struct write_flux_opts {
        const char *filename;
        bool image_info_only;
        int track;
        int head;
};

void write_flux_opts_print_usage(char * const argv[]);
bool write_flux_opts_parse(struct write_flux_opts *opts, int argc, char * const argv[]);

#endif // WRITE_FLUX_OPTS_H
