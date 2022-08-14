#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <endian.h>
#include <time.h>
#include <unistd.h>

#include "pru-setup.h"
#include "flux_data.h"
#include "read_flux.h"

extern struct pru * pru;

struct sector_samples {
        uint32_t *samples; // Pointer to first sample of sync marker (0x4489)
        int samples_count;
        int sector_number;
};
struct track_samples {
        uint32_t *samples; // Raw sample data, must be free'd!
        int sample_count;
        struct sector_samples sectors[11];
};

static void samples_to_bitsream(struct track_samples *track);
static void find_sync_marker(struct track_samples *track);

int read_flux(int argc, char ** argv)
{
        int i, r, opt, filename_index = 0;
        uint8_t start_track = 0;
        uint8_t end_track = 159;
        uint8_t revolutions = 1;
        uint32_t *index_offsets;
        uint16_t *converted_data;
        struct scp_rev_timing *timing;
        struct flux_data *flux_data;

        while((opt = getopt(argc, argv, "-r:")) != -1) {
                switch(opt) {
                case 'r':
                        revolutions = strtol(optarg, NULL, 0);
                        if (revolutions < 1) revolutions = 1;
                        if (revolutions > 64) revolutions = 64;
                        break;
                case 1:
                        filename_index = optind - 1;
                }
        }

#if 0
        if (!filename_index) {
                fprintf(stderr,
                        "You must specify a filename for the scp file\n");
                return -1;
        }
#endif

        pru_start_motor(pru);
        pru_reset_drive(pru);


        pru_set_head_dir(pru, PRU_HEAD_INC);

        struct track_samples track = {0};

        for (i = 0; i < 6; i++) {
                printf("Read track: %d, head: %d\n", i >> 1, i & 1);
                if (i % 2) {
                        pru_set_head_side(pru, PRU_HEAD_LOWER);
                } else {
                        pru_set_head_side(pru, PRU_HEAD_UPPER);
                }

                track.sample_count = pru_read_timing(pru, &track.samples, revolutions, &index_offsets);
                find_sync_marker(&track);
                samples_to_bitsream(&track);
                
                //r = revolutions - 1;
                //flux_data = flux_data_init(samples, index_offsets[r], index_offsets, revolutions);

                free(track.samples);
                // Track data is invalid now!
                memset(&track, 0x00, sizeof(track));

                free(index_offsets);

                printf("Read done! - \n");

                //samples2scp(&converted_data, &timing, samples, revolutions,
                //                                        index_offsets);

                printf("Converted! - \n");

                //add_scp_track(file, i, converted_data, timing);
                printf("Written\n");
                //free(timing);
                //free(converted_data);

                if (i % 2) {
                        pru_step_head(pru, 1);
                        printf("Step!\n");
                }
                //flux_data_free(flux_data);
        }
        pru_stop_motor(pru);

        return 0;
}

void hexdump(const void *b, size_t len);

/**
 * @brief       Convert an array of timing values to an array of raw data
 */
static void samples_to_bitsream(struct track_samples *track)
{
        // Maximum bits per sample is 4.
        const int bit_count = track->sample_count * 4;
        const int byte_count = 1 + (bit_count / 8);
        uint8_t *bitstream = malloc(byte_count);
        if (!bitstream) {
                fprintf(stderr, "%s: malloc failed!\n", __func__);
                return;
        }
        memset(bitstream, 0x00, byte_count);

        printf("Timing to bitstream, allocated %d bytes for %d bits\n", byte_count, bit_count);

        unsigned int bit = -1;
        for (int i = 0; i < track->sample_count; i++) {
                if (track->samples[i] > 700) {
                        bit += 4;
                } else if (track->samples[i] > 500) {
                        bit += 3;
                } else {
                        bit += 2;
                }
                bitstream[bit / 8] |= (1 << (bit % 8));
        }

        //hexdump(bitstream, 512);
        printf("Timing to bitstream, conversion done for %d bits\n", bit);

        free(bitstream);
}

/**
 * @brief       Find sync markers (0x4489) in the timing sample data
 *              Setup the pointers to for the 11 sectors in the track struct.
 *              The pointers will point to the first sample of the sync marker
 *              in the sector.
 */
static void find_sync_marker(struct track_samples *track)
{
        /**
         * A propper flux bit cell is mostly 4 bits.
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

        int sector = 0;

        for (int i = 0; i < track->sample_count; i++) {
                if (track->samples[i] > 700) {
                        ring_buffer[i % 10] = MS8;
                } else if (track->samples[i] > 500) {
                        ring_buffer[i % 10] = MS6;
                } else {
                        ring_buffer[i % 10] = MS4;
                }

                int r_index = (i - 10);
                // Wait until the ringbuffer is full before we start comparing.
                if (r_index < 0) continue;

                bool found = true;;
                for (int e = 0; e < 10; e++) {
                        if (sync[e] != ring_buffer[(r_index + e) % 10]) {
                                found = false;
                                break;
                        }
                }
                if (found) {
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
                }
        }
}
