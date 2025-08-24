#include "read_flux_opts.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

void read_flux_opts_print_usage(char * const argv[])
{
        printf("usage: %s <IPF-FILE>\n", argv[0]);
}

bool read_flux_opts_parse(struct read_flux_opts *opts, int argc, char * const argv[])
{
        opts->filename = NULL;

        do {
                switch(getopt(argc, argv, "-:it:h:")) {
                case 1:
                        opts->filename = optarg;
                        break;
                case '?':
                        fprintf(stderr, "Unknown argument: -%c\n", optopt);
                        return false;
                }

        } while(optind < argc);

        if (!opts->filename) {
                fprintf(stderr, "Missing required argument <IPF-FILE>\n");
                return false;
        }

        return true;
}

