#include "write_flux_opts.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

void write_flux_opts_print_usage(char * const argv[])
{
        printf("usage: %s [OPTION]... <IPF-FILE>\n", argv[0]);
        printf("\n");
        printf("  -i              only print ipf image info\n");
        printf("  -t <track>      Track number [0-83]\n");
        printf("  -h <head>       Head lower/upper [0|1]\n");
}

bool write_flux_opts_parse(struct write_flux_opts *opts, int argc, char * const argv[])
{
        opts->filename = NULL;
        opts->track = -1;
        opts->head = -1;
        opts->image_info_only = false;

        long strtol_res = -1;
        char *endptr = NULL;

        do {
                switch(getopt(argc, argv, "-:it:h:")) {
                case 'i':
                        opts->image_info_only = true;
                        break;
                case 't':
                        strtol_res = strtol(optarg, &endptr, 0);
                        if (optarg == endptr) {
                                fprintf(stderr, "Unknown track number: %s\n", optarg);
                                return false;
                        }
                        opts->track = strtol_res;
                        break;
                case 'h':
                        strtol_res = strtol(optarg, &endptr, 0);
                        if (optarg == endptr) {
                                fprintf(stderr, "Unknown head: %s\n", optarg);
                                return false;
                        }
                        opts->head = strtol_res;
                        break;
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

        if (opts->track != -1) {
                printf("Using track %d only\n", opts->track);
        }

        if (opts->head != -1) {
                if (opts->head > 1) {
                        opts->head = 1;
                } else if (opts->head < 0) {
                        opts->head = 0;
                }
                printf("Using head %d only\n", opts->head);
        }

        return true;
}

