#include "read_flux_simple.h"

#include "pru-setup.h"
#include "flux_data.h"
#include "read_flux_opts.h"
#include "mfm_utils/mfm_utils.h"
#include "caps_parser/caps_parser.h"

#include <assert.h>
#include <endian.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>

void hexdump(const void *b, size_t len);
extern struct pru * pru;

struct sector_samples {
        uint32_t *samples; // Pointer to first sample of sync marker (0x4489)
        int samples_count;
        int sector_number;
};
struct track_samples {
        uint32_t *samples; // Raw sample data, heap allocated
        int sample_count;
        struct sector_samples sectors[11];
};

static bool find_sync_marker(struct track_samples *track, size_t *index);
static uint8_t __attribute__((__unused__)) * samples_to_bitsream(
                struct track_samples *track, size_t index, size_t *byte_count);
static size_t timing_sample_to_bitstream(const uint32_t * restrict samples, size_t samples_count,
                                        uint8_t * restrict bitstream, size_t bitstream_size);

int read_flux_simple(int argc, char ** argv)
{
        uint8_t revolutions = 1;
        uint32_t *index_offsets;

        int rc = 0;
        struct read_flux_opts opts = {0};
        bool success = read_flux_opts_parse(&opts, argc, argv);
        if (!success) {
                read_flux_opts_print_usage(argv);
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

        /**
         * This buffer is used to store all the databytes of the mfm track.
         */
        uint8_t *disk_track_mfm_bitstream = malloc(1088 * 11); // Amiga mfm sector byte size times 11 sectors.
        if (!disk_track_mfm_bitstream) {
                rc = -1;
                fprintf(stderr, "Could not allocate bitstream buffer!\n");
                goto mfm_sector_bitstream_failed;
        }


        pru_start_motor(pru);
        pru_reset_drive(pru);

        pru_set_head_dir(pru, PRU_HEAD_INC);
        pru_step_head(pru, 3);

        struct track_samples track = {0};

        unsigned i = 0;
        pru_set_head_side(pru, i & 1 ? PRU_HEAD_LOWER : PRU_HEAD_UPPER);

        // Clear the buffer to hold a new track.
        memset(disk_track_mfm_bitstream, 0xaa, 1088 * 11);
        /**
         * The call to pru_read_timing will allocate a buffer for samples.
         * Our track.samples variable will point to this buffer.
         * We own this buffer after this call!
         * The samples are count * 10 nSecs per flux transition
         */
        track.sample_count = pru_read_timing(pru, &track.samples, revolutions, &index_offsets);
        size_t index = 0;
        struct amiga_sector sector;

        uint8_t *mfm_bitstream_ptr = disk_track_mfm_bitstream;

        /**
         * our timing_sample_to_bitstream(..) function starts by writing
         * the sync bytes (44 89 44 89).
         * So we skip four bytes ahead to where the sync bytes will be written.
         */
        mfm_bitstream_ptr += 4;

        if ( find_sync_marker(&track, &index) ) {
                printf("sync found @ index: %u\n", index);

                size_t consumed = timing_sample_to_bitstream(
                                track.samples + index,
                                track.sample_count - index,
                                mfm_bitstream_ptr, (1088 * 11) - 4);
                printf("mfm sector used %u samples\n", consumed);

                int rc = parse_amiga_mfm_sector(mfm_bitstream_ptr, 1084, &sector, NULL /* Don't keep sector data */);
                if (rc == 0) {
                        const uint8_t track_info = (be32toh(sector.header_info) >> 16) & 0xff;
                        const uint8_t sector_no = (be32toh(sector.header_info) >> 8) & 0xff;
                        //const uint8_t sector_to_gap = be32toh(sector.header_info) & 0xff;
                        //wprintw(w, "-- [I] Track: %d - head: %d\n", track_info >> 1, track_info & 1);
                        uint8_t sector_status = 0;
                        if (!sector.data_checksum_ok) {
                                sector_status |= 2;
                        }
                        if (!sector.header_checksum_ok) {
                                sector_status |= 1;
                        }

                        printf("Parsed: track: %u - sector: %u - status: %x\n",
                                        track_info, sector_no, sector_status);

                        FILE *fp = fopen("raw_bitstream", "wb");
                        if (fp) {
                                fwrite(disk_track_mfm_bitstream, 1, 1088 * 11, fp);
                                fclose(fp);
                        } else {
                                fprintf(stderr, "No dump!\n");
                        }

                }

                /**
                 * Move the index pointer, so we don't find the same
                 * sector sync marker on next iteration.
                 */
                index += 10;
        } else {
                fprintf(stderr, "\t\tNo sync markers found in track %d - skipping\n", i);
        }

        printf(" ---------------- Track Done ---------------------\n");

        free(track.samples);
        // Track data is invalid now!
        memset(&track, 0x00, sizeof(track));

        free(index_offsets);

        pru_stop_motor(pru);

        free(disk_track_mfm_bitstream);

mfm_sector_bitstream_failed:
        caps_parser_cleanup(parser);

caps_init_failed:
        fclose(ipf_img);

fopen_failed:

        return rc;
}

/**
 * @brief       Convert an array of timing values to an array of raw data
 *
 * @detail      struct track_samples contain all samples (timing data) read from
 *              a single track. We use index as a pointer to a specific sample to start
 *              converting to bitstream, based on a 2us bit cell size.
 *
 * @deprecated  Use timing_sample_to_bitstream instead!
 */
static uint8_t *samples_to_bitsream(struct track_samples *track, size_t index, size_t *_byte_count)
{
        *_byte_count = 0; // return paramater.

        // Maximum bits per sample is 4.
        const int bit_count = (track->sample_count - index) * 4;
        const int byte_count = 1 + (bit_count / 8);
        uint8_t *bitstream = malloc(byte_count);
        if (!bitstream) {
                fprintf(stderr, "%s: malloc failed!\n", __func__);
                return NULL;
        }
        memset(bitstream, 0x00, byte_count);

        //printf("Timing to bitstream, allocated %d bytes for %d bits\n", byte_count, bit_count);

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
        for (int i = index; i < track->sample_count; i++) {
                if (track->samples[i] > 700) {
                        bit += 4;
                } else if (track->samples[i] > 500) {
                        bit += 3;
                } else {
                        bit += 2;
                }
                bitstream[bit / 8] |= (1 << (7 - (bit % 8)));
        }

        //hexdump(bitstream, 512);
        //printf("Timing to bitstream, conversion done for %d bits\n", bit);
        *_byte_count = byte_count;
        return bitstream;
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
static bool find_sync_marker(struct track_samples *track, size_t *index)
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

        for (int i = *index; i < track->sample_count; i++) {
                if (track->samples[i] > 700) {
                        ring_buffer[i % 10] = MS8;
                } else if (track->samples[i] > 500) {
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
                        *index = i - 10;
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
