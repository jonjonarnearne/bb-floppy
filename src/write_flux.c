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

#include "write_flux_opts.h"
#include "caps_parser/caps_parser.h"
#include "pru-setup.h"

extern struct pru * pru;

static void write_data_to_disk(const struct write_flux_opts *opts, struct caps_parser *parser);
static size_t bitstream_to_timing_samples(uint16_t ** timing_data, const uint8_t *bitstream, size_t track_size);

/**
 * @brief       Entry point. Called from main.c
 */
int write_flux(int argc, char ** argv)
{
        int rc = 0;

        struct write_flux_opts opts = {0};
        bool success = write_flux_opts_parse(&opts, argc, argv);
        if (!success) {
                write_flux_opts_print_usage(argv);
                goto fopen_failed;
        }

        FILE *ipf_img = fopen(opts.filename, "rb");
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

        pru_start_motor(pru);
        pru_reset_drive(pru);
        pru_set_head_dir(pru, PRU_HEAD_INC);

        write_data_to_disk(&opts, parser);

        pru_stop_motor(pru);

        caps_parser_cleanup(parser);

caps_init_failed:
        fclose(ipf_img);

fopen_failed:
        return rc;
}

static void write_data_to_disk(const struct write_flux_opts *opts, struct caps_parser *parser)
{
        const struct CapsImage *track_info = NULL;

        unsigned last_track = (opts->track == -1 ? 79 : opts->track) * 2;
        unsigned track = opts->track == -1 ? 0 : opts->track * 2;

        if (opts->head == -1) {
                last_track += 2;
        } else if (opts->head == 1) {
                track++;
                last_track++;
        }

        do {
                uint8_t head = track & 0x01;
                uint8_t cylinder = track / 2;

                bool ret = caps_parser_get_caps_image_for_track_and_head(
                                        parser, &track_info, cylinder, head);
                if (!ret) {
                        fprintf(stderr,
                                "Could not find track %u - head %u in ipf file: %s\n",
                                                cylinder, head, opts->filename);
                        return;
                }

                size_t track_size = be32toh(track_info->trkbits) >> 3; // Div. 8 to get bytes.

                // I get a bitstream buffer here, which I have to free!
                // This call will call the internal `parse_ipf_samples` function to convert from IPF samples to MFM bitstream
                uint8_t *bitstream = caps_parser_get_bitstream_for_track(parser, track_info);

                /*
                for (int i = 0; i < 11; ++i) {
                        hexdump(bitstream + (1088 * i), 16); // For bug detection -  look for 0x2a here!
                }
                */

                uint16_t *timing_data = NULL;
                printf("-------------------------------------\nRead track: %u, head: %u\n", cylinder, head);
                size_t data_len = bitstream_to_timing_samples(&timing_data, bitstream, track_size);

                free(bitstream);
                if (data_len == 0) {
                        break;
                }

                pru_set_head_side(pru, head & 1 ? PRU_HEAD_LOWER : PRU_HEAD_UPPER);
                pru_write_timing(pru, timing_data, data_len);

                free(timing_data);

                track += opts->head == -1 ? 1 : 2;
                if (opts->head != -1 || track % 2 == 0) {
                        pru_step_head(pru, 1);
                }
        } while(track < last_track);
}

static size_t bitstream_to_timing_samples(uint16_t ** timing_data, const uint8_t *bitstream, size_t track_size)
{
        uint16_t *samples = malloc(sizeof(*samples) * (1u << 16));
        if (!samples) {
                return 0;
        }

        int sample_index = 0;

        unsigned bit_count = 0;
        uint8_t counter = 0;

        for (size_t i = 0; i < track_size; ++i) {
                const uint8_t b = bitstream[i];
                uint8_t bit = 0x80;
                while(bit) {
                        counter++;
                        if (b & bit) {
                                // printf("Count: %u\n", counter);
                                if (counter == 4) {
                                        samples[sample_index++] = 800;
                                } else if (counter == 3) {
                                        samples[sample_index++] = 600;
                                } else if (counter == 2) {
                                        samples[sample_index++] = 400;
                                }
                                counter = 0;
                                bit_count++;
                        }
                        bit >>= 1;
                        if (sample_index >= (1u << 16)) {
                                fprintf(stderr, "Stream index: %d / %d\n", i, track_size);
                                goto error_buffer_overflow;
                        }
                }
        }

        printf("Bit count: %u (samples: %d)\n", bit_count, sample_index);
        *timing_data = samples;
        return sample_index;

error_buffer_overflow:

        fprintf(stderr, "Sample buffer overflow!");
        free(samples);
        return 0;
}
