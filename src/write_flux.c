#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <endian.h>
#include <time.h>
#include <unistd.h>
#include <ncurses.h>
#include <errno.h>

#include "caps_parser/caps_parser.h"
#include "pru-setup.h"

/**
 * @brief       Entry point. Called from main.c
 */
int write_flux(int argc, char ** argv)
{
        int rc = 0;

        FILE *ipf_img = fopen("/home/root/IPF-Images/Lemmings2_Disk1.ipf", "rb");
        if (!ipf_img) {
                rc = -1;
                fprintf(stderr, "Could not open disk image. Error: %s\n", strerror(errno));
                goto fopen_failed;
        }

        struct caps_parser *parser = caps_parser_init(ipf_img);
        if (!parser) {
                rc = -1;
                fprintf(stderr, "Could not initialize caps_parser\n");
                goto caps_init_failed;

        }

        printf( "Write Flux called!\n"
                "Note - Hardcoded for \"/home/root/IPF-Images/Lemmings2-Disk1.ipf\"\n"
        );

        const struct CapsImage * track_data = NULL;
        bool ret = caps_parser_get_caps_image_for_track_and_head(parser, &track_data, 0,0);
        if (ret) {
                caps_parser_print_caps_image(track_data);
                uint8_t *bitstream = caps_parser_get_bitstream_for_track(parser, track_data);
                free(bitstream);
        } else {
                fprintf(stderr, "Failed to find track 0 head 0 in ipf image!\n");
        }

        caps_parser_cleanup(parser);
caps_init_failed:

        fclose(ipf_img);
fopen_failed:

        return rc;
}
