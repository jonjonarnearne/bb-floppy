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
#include "mfm_utils/mfm_utils.h"
#include "caps_parser/caps_parser.h"
#include "pru-setup.h"

extern struct pru * pru;

static void write_data_to_disk(const struct write_flux_opts *opts, struct caps_parser *parser);
static void verify_bitstream(const uint8_t *bitstream);
static size_t bitstream_to_timing_samples(uint16_t ** timing_data, const uint8_t *bitstream, size_t track_size);
static void verify_read_samples(uint32_t *samples, int sample_count);

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

        if (opts->track > 0) {
                pru_step_head(pru, opts->track);
        }

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

                // TODO: Verify that the bitstream is actually correct in transitions between sectors!
                //       If the last byte of sector 0 has last bit set, we can not have 0xaa in the gap!
                verify_bitstream(bitstream);
                /*
                for (int i = 0; i < 11; ++i) {
                        hexdump(bitstream + (1088 * i), 16); // For bug detection -  look for 0x2a here!
                }
                */

                // TODO: Investigate single bit fault.
                //       Consistently reproduced in MonkeyIsland2_Disk1.ipf -t0 -h0
                //       In amiga sector 4 at sector index 9.
                //       Offset 0x1dc - Got 0x51 expected 0x55
                //       Sample index: 0x8970
                // 
                //       Sometimes reproduced in MonkeyIsland2_Disk1.ipf -t0 -h1
                //       In amiga sector 9 at sector index 9.
                //       Offset 0x1ed - Got 0x11 expected 0x15
                //       Sample index: 0x8330

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

                uint32_t *index_offsets;
                uint32_t *samples;
                int sample_count = pru_read_timing(pru, &samples, 1, &index_offsets);

                verify_read_samples(samples, sample_count);

                free(index_offsets);
                free(samples);

                track += opts->head == -1 ? 1 : 2;
                if (opts->head != -1 || track % 2 == 0) {
                        pru_step_head(pru, 1);
                }
        } while(track < last_track);
}

static void verify_bitstream(const uint8_t *bitstream)
{
        struct amiga_sector sector;
        for (int i = 0; i < 11; ++i) {
                const uint8_t *sector_bits = bitstream + (1088 * i);
                int rc = parse_amiga_mfm_sector(sector_bits + 4, 1084, &sector, NULL /* Don't keep sector data */);
                if (rc == 0 && (!sector.data_checksum_ok || !sector.header_checksum_ok)) {
                        const uint8_t track_info = (be32toh(sector.header_info) >> 16) & 0xff;
                        const uint8_t sector_no = (be32toh(sector.header_info) >> 8) & 0xff;
                        //const uint8_t sector_to_gap = be32toh(sector.header_info) & 0xff;
                        printf("Hexdump: sector %d\n", i);
                        hexdump(bitstream + (1088 * i), 16); // For bug detection -  look for 0x2a here!
                        printf("-- [I] Track: %d - head: %d - sector: %u - data_ok: %s - header_ok: %s\n",
                                        track_info >> 1, track_info & 1, sector_no,
                                        sector.data_checksum_ok ? "YES" : "NO",
                                        sector.header_checksum_ok ? "YES" : "NO");
                }
        }
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

static bool find_sync_in_read_samples(const uint32_t *samples, int count, int *found_index);
static size_t timing_sample_to_bitstream(const uint32_t * restrict samples, size_t samples_count,
                                        uint8_t * restrict bitstream, size_t bitstream_size);

#define CLEAR "\033[0m"
#define RED "\033[0;31m"
/**
 * Check that the newly written bitstream is correct!
 */
static void verify_read_samples(uint32_t *samples, int sample_count)
{
        // We must convert the samples (flux timing to a bitstream)
        uint8_t *bitstream = malloc(1088 * 11);
        if (!bitstream) {
                fprintf(stderr, RED "Could not malloc bitstream for track verification!\n" CLEAR);
                return;
        }

        int i = 0;
        bool rc = find_sync_in_read_samples(samples, sample_count, &i);
        if (!rc) {
                fprintf(stderr, RED "No sync marker found in track\n" CLEAR);
                return;
        }

        // printf("Found sync in read samples at index: %d\n", i);

        bitstream[0] = 0xaa;
        bitstream[1] = 0xaa;
        bitstream[2] = 0xaa;
        bitstream[3] = 0xaa;

        size_t consumed = timing_sample_to_bitstream(
                samples + i,
                sample_count - i,
                bitstream + 4, (1088 * 11) - 4);

        (void) consumed;
        // printf("Consumed %d samples\n", consumed);

        /*
        hexdump(bitstream, 16);
        hexdump(bitstream + 1084, 16);
        */

        // Now convert the bitstream to amiga sectors

        for (int i = 0; i < 11; ++i) {
                struct amiga_sector sector;
                int parse_ok = parse_amiga_mfm_sector(bitstream + (1088 * i) + 4, 1084, &sector, NULL /* Don't keep sector data */);
                if (parse_ok == 0 && (!sector.data_checksum_ok || !sector.header_checksum_ok)) {
                        const uint8_t track_info = (be32toh(sector.header_info) >> 16) & 0xff;
                        const uint8_t sector_no = (be32toh(sector.header_info) >> 8) & 0xff;
                        //const uint8_t sector_to_gap = be32toh(sector.header_info) & 0xff;
                        printf("Hexdump: sector %d after index\n", i);
                        hexdump(bitstream + (1088 * i), 1096);
                        printf("-- [I] Track: %d - head: %d - sector: %u - data_ok: %s - header_ok: %s\n",
                                        track_info >> 1, track_info & 1, sector_no,
                                        sector.data_checksum_ok ? "YES" : "NO",
                                        sector.header_checksum_ok ? "YES" : "NO");
                } else if (parse_ok != 0) {
                        fprintf(stderr, RED "Could not parse amiga sector: %i\n" CLEAR, i);
                }
        }

#if 0
        struct amiga_sector sector;
        int parse_ok = parse_amiga_mfm_sector(bitstream + 4, 1084, &sector, NULL /* Don't keep sector data */);
        if (parse_ok == 0 && (!sector.data_checksum_ok || !sector.header_checksum_ok)) {
                const uint8_t track_info = (be32toh(sector.header_info) >> 16) & 0xff;
                const uint8_t sector_no = (be32toh(sector.header_info) >> 8) & 0xff;
                //const uint8_t sector_to_gap = be32toh(sector.header_info) & 0xff;
                printf("Hexdump: sector %d\n", i);
                hexdump(bitstream + (1088 * i), 16); // For bug detection -  look for 0x2a here!
                printf("-- [I] Track: %d - head: %d - sector: %u - data_ok: %s - header_ok: %s\n",
                                track_info >> 1, track_info & 1, sector_no,
                                sector.data_checksum_ok ? "YES" : "NO",
                                sector.header_checksum_ok ? "YES" : "NO");
        }
#endif

        free(bitstream);
}

/**
 * @detail      Parse `samples_count` flux timing data into a bitstream, based on
 *              a hardcoded bitcell time of 2 microseconds.
 *              The bitsrem is written into the given `bitstream` pointer until
 *              either we are out of samples to read or the bitstream buffer is full.
 */
static size_t timing_sample_to_bitstream(const uint32_t * restrict samples, size_t samples_count,
                                        uint8_t * restrict bitstream, size_t bitstream_size)
{
        // clear bitstream..
        memset(bitstream, 0x00, bitstream_size);
        /**
         * We start at bit -2 as this is what the bitstream will look like from
         * the index we found for sync:
         *
         *     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
         *    01010001001000100101000100100010010
         *     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
         *
         * While the actual sync mark are the bits in the box.
         *
         *   xx|01000100100010010100010010001001|x
         *
         * Without the -2 here, the data will be offset by one bit.
         *
         * TODO:
         * Investigate if we can set `bit` to 0 and just change
         * the index we return from find_sync_marker(...)
         *
         */
        unsigned int bit = -2; // See Note ^^^
        size_t i = 0;
        for (; i < samples_count; i++) {
                if (samples[i] > 700) {
                        bit += 4;
                } else if (samples[i] > 500) {
                        bit += 3;
                } else {
                        bit += 2;
                }
                const int byte_no = bit >> 3;  // Div. 8
                const int bit_no = bit & 0x07; // Mod. 8
                if (byte_no >= bitstream_size) {
                        // Bitstream full.
                        return i + 1;
                }
                bitstream[byte_no] |= (1 << (7 - (bit_no)));
        }

        return i;
}

/**
 * @brief       Find sync markers (0x4489) in the timing sample data
 *              Setup the pointers to for the 11 sectors in the track struct.
 *              The pointers will point to the first sample of the sync marker
 *              in the sector.
 */
static bool find_sync_in_read_samples(const uint32_t *samples, int count, int *found_index)
{
        /**
         * A the most bits between flux transitions is 4
         * There are only three valid flux values:
         *
         *      01      This is 4 micro-seconds
         *      001     This is 6 micro-seconds
         *      0001    This is 8 micro-seconds.
         */
        enum sample_type { UNDEF, MS4, MS6, MS8 };

        /**
         * This is the series of flux transion timing data
         * we are looking for.
         * The following is the timing values to
         * encode 0x44894489 as MFM data timing.
         */
        // TODO: I'd assume the first sample should be MS6 instead of MS4
        //       as 0xaa is preceeding the sync marker so the preceding bits were 0b10
        static const enum sample_type sync[10] = {
                MS4, MS8, MS6, MS8, MS6, // 0x4489
                MS4, MS8, MS6, MS8, MS6  // 0x4489
        };

        /**
         * In this loop, we maintain the 10 latest
         * flux timing values in a ring buffer,
         * and compare it against the sync buffer
         * to see if they match.
         */
        enum sample_type ring_buffer[10] = {UNDEF};

        /*
         * Multiply count with 10 to get nSec
         * Divide by 100 to get μSec
         */

        //int sector = 0;

        for (int i = 0; i < count; ++i) {
                if (samples[i] > 700) {
                        ring_buffer[i % 10] = MS8;
                } else if (samples[i] > 500) {
                        ring_buffer[i % 10] = MS6;
                } else {
                        ring_buffer[i % 10] = MS4;
                }

                int r_index = (i - 10);
                // Wait until the ringbuffer is full before we start comparing.
                if (r_index < 0) {
                        continue;
                }

                bool found = true;
                for (int e = 0; e < 10; e++) {
                        if (sync[e] != ring_buffer[(r_index + e) % 10]) {
                                found = false;
                                break;
                        }
                }
                if (found) {
                        *found_index = i - 10;
                        return true;
                        /*
                        if (sector >= 11) {
                                // Current implementation only expects 11 sectors!
                                fprintf(stderr,
                                "%s: Error, found sector marker for sector: %d\n",
                                                        __func__, sector + 1);
                        } else {
                                // Setup pointers to start of sector in the track struct.
                                track->sectors[sector].samples =
                                                        &track->samples[i - 10];
                                printf(
                                "Sync for sector %d found at sample inndex: %d\n",
                                                                sector + 1, i);
                        }
                        sector++;
                        */
                }
        }
        return false;
}
