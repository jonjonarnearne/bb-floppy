#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <endian.h>

#include "mfm.h"
#include "read_track_timing.h"
#include "pru-setup.h"

extern void usage(void);
extern struct pru * pru;

static void shift(void *d, int len, uint8_t bit)
{
	int i;
        uint8_t *data = d;
	for(i = 0; i < len-1; i++) {
		data[i] <<= 1;
		data[i] |= (data[i+1] & 0x80) >> 7;
	}
	data[len - 1] <<= 1;
	if (bit) data[len - 1] |= 1;
}

static int check_sync(struct raw_mfm_sector *sector)
{
        if (sector->sync_word == 0x89448944) {
                return 1;
        }
	return 0;
}

/* Parse timing info, and return <sector_count> sectors in <sectors> ptr.
 * */
static int find_std_sector_headers(const uint16_t *timing, int sample_count,
                struct mfm_sector_header * headers, int header_count)
{
        struct raw_mfm_sector *raw_mfm_sectors, *cur_sector;
	int i,e, c, count = header_count;
        uint32_t chksum;

        if (!header_count)
                return 0;

	raw_mfm_sectors = malloc(count * sizeof(*raw_mfm_sectors));
	if (!raw_mfm_sectors) {
		fprintf(stderr, "Couldn't allocate memory for buffer!\n");
		return 0;
	}
	memset(raw_mfm_sectors, 0x00, count * sizeof(*raw_mfm_sectors));

	c = count;
        cur_sector = raw_mfm_sectors;
	for(i=0; i < sample_count; i++) {
		if (timing[i] > 720) {
			shift(cur_sector, sizeof(*cur_sector), 0);
			if (check_sync(cur_sector)) {
				if (!--count) break;
                                cur_sector++;
                        }
		}
		if (timing[i] > 540) {
			shift(cur_sector, sizeof(*cur_sector), 0);
			if (check_sync(cur_sector)) {
				if (!--count) break;
                                cur_sector++;
                        }
		}
		shift(cur_sector, sizeof(*cur_sector), 0);
		if (check_sync(cur_sector)) {
			if (!--count) break;
                        cur_sector++;
                }

		shift(cur_sector, sizeof(*cur_sector), 1);
		if (check_sync(cur_sector)) {
			if (!--count) break;
                        cur_sector++;
                }
	}

	c -= count;
	header_count = (header_count < c) ? header_count : c;

        cur_sector = raw_mfm_sectors;
        for(i=0; i < header_count; i++) {
                chksum = 0; 
                headers->info = cur_sector->odd_info & MFM_DATA_MASK;
                headers->info <<= 1;
                headers->info |= cur_sector->even_info & MFM_DATA_MASK;

                chksum ^= cur_sector->odd_info;
                chksum ^= cur_sector->even_info;
                chksum &= MFM_DATA_MASK;

                for(e=0; e<4; e++) {
                        headers->label[i] = 
                               cur_sector->odd_label[i] & MFM_DATA_MASK;
                        headers->label[i] <<= 1;
                        headers->label[i] |=
                                cur_sector->even_label[i] & MFM_DATA_MASK;

                        chksum ^= cur_sector->odd_label[i];
                        chksum ^= cur_sector->even_label[i];
		}
                chksum &= MFM_DATA_MASK;

                headers->header_chksum =
                                cur_sector->odd_h_chksum & MFM_DATA_MASK;
                headers->header_chksum <<= 1;
                headers->header_chksum =
                                cur_sector->even_h_chksum & MFM_DATA_MASK;

                headers->head_chksum_ok =
				(chksum == headers->header_chksum);
		chksum = 0;
		for(e = 0; e < 512/4; e++) {
			headers->data[e] =
				cur_sector->odd_data[e] & 0x55;
			headers->data[e] <<= 1;
			headers->data[e] |=
				cur_sector->even_data[e] & 0x55;
			chksum ^= cur_sector->odd_data[e];
			chksum ^= cur_sector->even_data[e];
		}
		chksum &= MFM_DATA_MASK;

                headers->data_chksum =
                                cur_sector->odd_d_chksum & MFM_DATA_MASK;
                headers->data_chksum <<= 1;
                headers->data_chksum =
                                cur_sector->even_d_chksum & MFM_DATA_MASK;

		headers->data_chksum_ok =
				(chksum == headers->data_chksum);
                headers++;
                cur_sector++;
        }

	free(raw_mfm_sectors);
	return header_count;
}

static void measure_samples(const uint16_t * samples, int sample_count)
{
        int i;
        int brackets[9] = {0};
        int bracket_count[9] = {0};
	int total_time = 0;

        for(i=0; i<sample_count; i++) {
		total_time += samples[i];
                if (samples[i] < 300) {
                        // DISCARD
                        continue;
                        
		// 01 
                } else if (samples[i] < 385) {
                        brackets[0] += samples[i];
                        bracket_count[0]++;
                } else if (samples[i] < 405) {
                        brackets[1] += samples[i];
                        bracket_count[1]++;
                } else if (samples[i] < 440) {
                        brackets[2] += samples[i];
                        bracket_count[2]++;
		// 001
                } else if (samples[i] < 585) {
                        brackets[3] += samples[i];
                        bracket_count[3]++;
                } else if (samples[i] < 610) {
                        brackets[4] += samples[i];
                        bracket_count[4]++;
                } else if (samples[i] < 660) {
                        brackets[5] += samples[i];
                        bracket_count[5]++;
		// 0001
                } else if (samples[i] < 780) {
                        brackets[6] += samples[i];
                        bracket_count[6]++;
                } else if (samples[i] < 820) {
                        brackets[7] += samples[i];
                        bracket_count[7]++;
                } else {
                        brackets[8] += samples[i];
                        bracket_count[8]++;
                }

        }

        printf("Short [01] --\n");
        printf("Bracket 0 (<385): %d samples, avg: %f\n", bracket_count[0],
                                (float)brackets[0]/(float)bracket_count[0]);
        printf("Bracket 1 (<405): %d samples, avg: %f\n", bracket_count[1],
                                (float)brackets[1]/(float)bracket_count[1]);
        printf("Bracket 2 (<440): %d samples, avg: %f\n", bracket_count[2],
                                (float)brackets[2]/(float)bracket_count[2]);
        printf("Total: %d samples\n", bracket_count[0]
                                  + bracket_count[1]
                                  + bracket_count[2]);

        printf("\nMedium [001] --\n");
        printf("Bracket 3 (<585): %d samples, avg: %f\n", bracket_count[3],
                                (float)brackets[3]/(float)bracket_count[3]);
        printf("Bracket 4 (<610): %d samples, avg: %f\n", bracket_count[4],
                                (float)brackets[4]/(float)bracket_count[4]);
        printf("Bracket 5 (<660): %d samples, avg: %f\n", bracket_count[5],
                                (float)brackets[5]/(float)bracket_count[5]);
        printf("Total: %d samples\n", bracket_count[3]
                                  + bracket_count[4]
                                  + bracket_count[5]);

        printf("\nLong [0001] --\n");
        printf("Bracket 6 (<780): %d samples, avg: %f\n", bracket_count[6],
                                (float)brackets[6]/(float)bracket_count[6]);
        printf("Bracket 7 (<820): %d samples, avg: %f\n", bracket_count[7],
                                (float)brackets[7]/(float)bracket_count[7]);
        printf("Bracket 8 (----): %d samples, avg: %f\n", bracket_count[8],
                                (float)brackets[8]/(float)bracket_count[8]);
        printf("Total: %d samples (%d)\n", bracket_count[6]
                                  + bracket_count[7]
                                  + bracket_count[8], sample_count);
	printf("Total time: %fus\n", (total_time * 10) / 1000.0);
}

static const uint16_t q_limits[] = { 108, 114, 140,
				     175, 185, 200,
				     236, 255,   0 };
static const uint16_t q_values[] = { 105, 110, 117,
				     168, 178, 189,
				     232, 245, 259 };
static void quantize_samples(uint16_t * samples, int sample_count)
{
	int i,e;
	const uint16_t *lim;
	for (i = 0; i < sample_count; i++) {
		lim = q_limits;
		e = 0;
		while(*lim) {
			if (samples[i] < *lim) break;
			e++;
			lim++;
		}
		samples[i] = q_values[e];
	}
}

int read_track_timing(int argc, char ** argv)
{
	int rc, i, opt, sample_count,
	    measure = 0, quantize = 0, count = 1;
	const char *fn = NULL;
	char *filename = NULL;
	FILE *fp;
        uint16_t *timing = NULL;
        uint32_t *offsets = NULL;
	enum pru_head_side track_side = PRU_HEAD_UPPER;
        struct mfm_sector_header *header, *h;


	while((opt = getopt(argc, argv, "-lMQC:")) != -1) {
		switch(opt) {
		case 'l':
			track_side = PRU_HEAD_LOWER;
			break;
		case 'M':
			measure = 1;
			break;
		case 'Q':
			quantize = 1;
			break;
		case 'C':
                        // Number of revolutions to sample
			count = strtol(optarg, NULL, 0);
			if (count < 1) count = 1;
			if (count > 64) count = 64;
			break;
		case 1:
			// If you specify a filename,
                        // we will save the data to that file.
			fn = argv[optind-1]; //optarg;
			printf("Filename detected: %s\n", fn);
		}
	}

        header = malloc(11 * count * sizeof(*header));
        if (!header) {
                fprintf(stderr, "Couldn't allocate header buffer\n");
                return 0;
        }

	if (fn) {
		filename = malloc(strlen(fn) + 5);
		if (!filename) {
			fprintf(stderr,
				"fatal -- Couldn't allocate memory for filename buffer!\n"
			);
			return -1;
		}
		if (count == 1) {
			strcpy(filename, fn);
		} else {
			sprintf(filename, "%s%02d", fn, count);
			//printf("filename: %s\n", filename);
		}
	}

        pru_start_motor(pru);
        pru_set_head_side(pru, track_side);
	sample_count = pru_read_timing(pru, &timing, count, &offsets);
	pru_stop_motor(pru);

        printf("\tGot %d samples\n", sample_count);

        if (quantize) {
                printf("Quantize!\n");
                quantize_samples(timing, sample_count);
        }

        if (measure) {
                measure_samples(timing, sample_count);
                /*
                printf("517: %03d %03d %03d %03d %03d\n"
                       "522: %03d %03d %03d %03d %03d\n"
                       "527: %03d %03d %03d %03d %03d\n",
                timing[517], timing[518], timing[519], timing[520],
                timing[521], timing[522], timing[523], timing[524],
                timing[525], timing[526], timing[527], timing[528],
                timing[529], timing[530], timing[531]);
                */
                
        }

        rc = find_std_sector_headers(timing, sample_count,
                                        header, 11 * count);
        printf("got %d headers\n", rc);

        if (!rc) {
                fprintf(stderr,
        "Couldn't find any standard sectors in data stream!\n"
                );
                printf("Offsets:\n");
                for (i = 0; i < 3; i++) {
                        printf("\t%d\n", offsets[i]);
                }
        } else {
                h = header;
                for (i = 0; i < rc; i++) {
                        if (i % 11 == 0) printf("---------------\n");
                        MFM_INFO_PRINT(h->info);
                        if (!h->head_chksum_ok)
                                printf("Head Chksum error!\n");
                        if (!h->data_chksum_ok)
                                printf("Data Chksum error!\n");
                        h++;
                }
        }

        if (filename) {
                fp = fopen(filename, "w");
                fwrite(timing, sizeof(*timing), sample_count, fp);
                fclose(fp);
                sprintf(filename, "%s%02d", fn, count - 1);
        }

	free(filename);
	free(timing);
	return 0;
}

int write_track_timing(int argc, char ** argv)
{
        uint16_t *timing;
        int rc, i, sample_count = 8192;//4096;

        timing = malloc(sample_count * sizeof(*timing));
        if (!timing) {
                fprintf(stderr, "Couldn't alloc memory for samples!\n");
                return -1;
        }

        for(i = 0; i < sample_count; i++) {
                // 1 == 115ns high
                // 135 + (sample * 10)
                timing[i] = 400; // 01 = 2us bit-cell
        }
        // 0x4489
        timing[500] = 400; 
        timing[501] = 800;
        timing[502] = 600;
        timing[503] = 800;
        timing[504] = 600;

        // 0x4489
        timing[505] = 400; 
        timing[506] = 800;
        timing[507] = 600;
        timing[508] = 800;
        timing[509] = 600;

        timing[510] = 400;
        timing[511] = 400;

        printf("Sending %d samples, (%d bytes)\n",
                                                sample_count,
                                sample_count * sizeof(*timing));
        pru_start_motor(pru);
	pru_reset_drive(pru);
        pru_set_head_dir(pru, PRU_HEAD_INC);
	rc = pru_write_timing(pru, timing, sample_count);
        pru_stop_motor(pru);

        printf("%d samples left in buffer\n", rc);


        return 1;
                            
#if 0
	FILE *fp;
	int rc, sample_count;
        long file_size;
	uint16_t *timing;

	if (argc != 2) {
		usage();
		printf("You must give a filename to a timing file\n");
		return -1;	
	}


	fp = fopen(argv[1], "r");
        fseek(fp, 0, SEEK_END);
        file_size = ftell(fp);
        sample_count = file_size / sizeof(*timing);
        rewind(fp);

        printf("Filesize: %ld\n", file_size);
        timing = malloc(file_size);
        if (!timing) {
                fprintf(stderr,
                        "fatal -- Couldn't allocate memory for file buffer!\n"
                );
                return -1;
        }

        printf("Going to read %d samples\n", sample_count);
	rc = fread(timing, sizeof(*timing), sample_count, fp);
        if (rc != sample_count) {
                fprintf(stderr, "fatal -- Couldn't read sample file\n");
                return -1;
        }
        pru_start_motor(pru);
	pru_reset_drive(pru);
        pru_set_head_dir(pru, PRU_HEAD_INC);
	rc = pru_write_bit_timing(pru, timing, sample_count);
        pru_stop_motor(pru);

        printf("Success! - Samples left: %d\n", rc);
	free(timing);
	return 0;

#endif
#if 0 
        c = 0;
	while(!feof(fp)) {
                if (c % 2)
                        pru_set_head_side(pru, PRU_HEAD_LOWER);
                else
                        pru_set_head_side(pru, PRU_HEAD_UPPER);

		d = fread(&sample_count, sizeof(sample_count), 1, fp);
		if (!d) continue;

	        //pru_write_bit_timing(pru, timing, sample_count);
                if (c % 2)
                        pru_step_head(pru, 1);
		c++;
	}
#endif

        //pru_stop_motor(pru);

}


/* read for 200000us (one complete track,
 * and store timing information to file
 * the timing is an array of unsigned 16-bit integers.
 * Multiply the numbers with 30, to get nano-seconds.
 * 
 * TODO: Implement full disk read that will read all timings,
 * and write to special container file.
 * The file structure must account for variable sample_count!
 * Then implement a function to write back the samples to a new disk.
 * */
int read_timing(int argc, char ** argv)
{
	FILE *fp;
        uint16_t *timing;
	int sample_count, i, e, rc;
        struct mfm_sector_header *header, *h;

        header = malloc(11 * sizeof(*header));
        if (!header) {
                fprintf(stderr, "Couldn't allocate buffer mem!\n");
                return 0;
        }

	if (argc != 2) {
		usage();
		printf("You must give a filename to a timing file\n");
		return -1;	
	}

	fp = fopen(argv[1], "w");

        pru_start_motor(pru);
	pru_reset_drive(pru);
        pru_set_head_dir(pru, PRU_HEAD_INC);

	for (i=0; i < 82 * 2; i++) {
		if (i & 1)
			pru_set_head_side(pru, PRU_HEAD_LOWER);
		else
			pru_set_head_side(pru, PRU_HEAD_UPPER);

		printf("Read track %d: head: %d :: ", i >> 1, i & 1);
		sample_count = pru_read_bit_timing(pru, &timing);
		if (!sample_count) {
			fprintf(stderr, "Got zero samples\n");
			break;
		}

		printf("Got %d samples\n", sample_count);
		rc = find_std_sector_headers(timing, sample_count, header, 11);
		if (!rc) {
			fprintf(stderr,
			"Couldn't find any standard sectors in data stream!\n"
			);
		} else if (MFM_INFO_GET_GAP(header->info) != 11) {
			fprintf(stderr,
				"warning: index not in gap - dist to gap: %d\n",
						MFM_INFO_GET_GAP(header->info)
			);
		} else {
			h = header;
			for (e = 0; e < rc; e++) {
				//MFM_INFO_PRINT(h->info);
				if (!h->head_chksum_ok)
					printf("Head Chksum error!\n");
				if (!h->data_chksum_ok)
					printf("Data Chksum error!\n");
				h++;
			}
		}

		fwrite(&sample_count, sizeof(sample_count), 1, fp);
		fwrite(timing, sizeof(*timing), sample_count, fp);

		free(timing);

		if (i & 1)
			pru_step_head(pru, 1);
	}
        pru_stop_motor(pru);

	free(header);
	fclose(fp);

        return 0;
}

int write_timing(int argc, char ** argv)
{
	FILE *fp;
	int sample_count, c=0, d=0;
	uint16_t *timing = malloc(45000 * sizeof(*timing));
        uint16_t val;

	if (!timing) {
		printf("%s: fatal -- Couldn't allocate memory for buffer!\n",
								argv[0]);
		return -1;	
	}

	if (argc != 2) {
		usage();
		printf("You must give a filename to a timing file\n");
		return -1;	
	}

        pru_start_motor(pru);
	pru_reset_drive(pru);
        pru_set_head_dir(pru, PRU_HEAD_INC);

	fp = fopen(argv[1], "r");

        c = 0;
	while(!feof(fp)) {
                if (c % 2)
                        pru_set_head_side(pru, PRU_HEAD_LOWER);
                else
                        pru_set_head_side(pru, PRU_HEAD_UPPER);

		d = fread(&sample_count, sizeof(sample_count), 1, fp);
		if (!d) continue;

		fread(timing, sizeof(*timing), sample_count, fp);
	        pru_write_bit_timing(pru, timing, sample_count);
                if (c % 2)
                        pru_step_head(pru, 1);
		c++;
	}
	printf("Got %d tracks\n", c);

        pru_stop_motor(pru);
	free(timing);

	return 0;
}

