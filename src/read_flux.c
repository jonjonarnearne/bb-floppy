#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <endian.h>
#include <time.h>
#include <unistd.h>

#include "pru-setup.h"
#include "flux_data.h"
#include "read_flux.h"

extern struct pru * pru;

int read_flux(int argc, char ** argv)
{
        int i, r, opt, filename_index = 0;
        uint8_t start_track = 0;
        uint8_t end_track = 159;
        uint8_t revolutions = 1;
        uint32_t *samples;
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
        for (i = 0; i < 6; i++) {
                printf("Read track: %d, head: %d\n", i >> 1, i & 1);
                if (i % 2) {
                        pru_set_head_side(pru, PRU_HEAD_LOWER);
                } else {
                        pru_set_head_side(pru, PRU_HEAD_UPPER);
                }

                int sample_count = pru_read_timing(pru, &samples, revolutions, &index_offsets);

                uint32_t max = 0;
                uint32_t min = 2000;
                for ( int i = 0; i < sample_count; i++ ) {
                        if (samples[i] > max) {
                                max = samples[i];
                        }
                        if (samples[i] < min) {
                                min = samples[i];
                        }
                }
                printf("max: %d - min: %d\n", max, min);

                //r = revolutions - 1;
                //flux_data = flux_data_init(samples, index_offsets[r], index_offsets, revolutions);
                free(samples);
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
