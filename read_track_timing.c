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
	int i,e, c, count = 11;
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
		if (timing[i] > 230) {
			shift(cur_sector, sizeof(*cur_sector), 0);
			if (check_sync(cur_sector)) {
				if (!--count) break;
                                cur_sector++;
                        }
		}
		if (timing[i] > 160) {
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

        for(i=0; i<sample_count; i++) {
		// 01 
                if (samples[i] < 107) {
                        brackets[0] += samples[i];
                        bracket_count[0]++;
                } else if (samples[i] < 115) {
                        brackets[1] += samples[i];
                        bracket_count[1]++;
                } else if (samples[i] < 140) {
                        brackets[2] += samples[i];
                        bracket_count[2]++;
		// 001
                } else if (samples[i] < 175) {
                        brackets[3] += samples[i];
                        bracket_count[3]++;
                } else if (samples[i] < 185) {
                        brackets[4] += samples[i];
                        bracket_count[4]++;
                } else if (samples[i] < 200) {
                        brackets[5] += samples[i];
                        bracket_count[5]++;
		// 0001
                } else if (samples[i] < 235) {
                        brackets[6] += samples[i];
                        bracket_count[6]++;
                } else if (samples[i] < 255) {
                        brackets[7] += samples[i];
                        bracket_count[7]++;
                } else {
                        brackets[8] += samples[i];
                        bracket_count[8]++;
                }

        }
        printf("Short [01] --\n");
        printf("Bracket 0 (<107): %d samples, avg: %f\n", bracket_count[0],
                                (float)brackets[0]/(float)bracket_count[0]);
        printf("Bracket 1 (<115): %d samples, avg: %f\n", bracket_count[1],
                                (float)brackets[1]/(float)bracket_count[1]);
        printf("Bracket 2 (<140): %d samples, avg: %f\n", bracket_count[2],
                                (float)brackets[2]/(float)bracket_count[2]);
        printf("Total: %d samples\n", bracket_count[0]
                                  + bracket_count[1]
                                  + bracket_count[2]);

        printf("\nMedium [001] --\n");
        printf("Bracket 3 (<175): %d samples, avg: %f\n", bracket_count[3],
                                (float)brackets[3]/(float)bracket_count[3]);
        printf("Bracket 4 (<185): %d samples, avg: %f\n", bracket_count[4],
                                (float)brackets[4]/(float)bracket_count[4]);
        printf("Bracket 5 (<200): %d samples, avg: %f\n", bracket_count[5],
                                (float)brackets[5]/(float)bracket_count[5]);
        printf("Total: %d samples\n", bracket_count[3]
                                  + bracket_count[4]
                                  + bracket_count[5]);

        printf("\nLong [0001] --\n");
        printf("Bracket 6 (<235): %d samples, avg: %f\n", bracket_count[6],
                                (float)brackets[6]/(float)bracket_count[6]);
        printf("Bracket 7 (<255): %d samples, avg: %f\n", bracket_count[7],
                                (float)brackets[7]/(float)bracket_count[7]);
        printf("Bracket 8 (----): %d samples, avg: %f\n", bracket_count[8],
                                (float)brackets[8]/(float)bracket_count[8]);
        printf("Total: %d samples\n", bracket_count[6]
                                  + bracket_count[7]
                                  + bracket_count[8]);
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
	int rc, i, opt, sample_count, measure = 0, quantize = 0;
	const char *filename = NULL;
	FILE *fp;
        uint16_t *timing = NULL;
	enum pru_head_side track_side = PRU_HEAD_UPPER;
        struct mfm_sector_header *header, *h;

        header = malloc(11 * sizeof(*header));
        if (!header) {
                fprintf(stderr, "Couldn't allocate buffer mem!\n");
                return 0;
        }

	while((opt = getopt(argc, argv, "-lMQ")) != -1) {
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
		case 1:
			// If you specify a filename,
                        // we will save the data to that file.
			filename = argv[optind-1]; //optarg;
			printf("Filename detected: %s\n", filename);
		}
	}

        pru_start_motor(pru);
        pru_set_head_side(pru, track_side);
	sample_count = pru_read_bit_timing(pru, &timing);
        pru_stop_motor(pru);

	printf("\tGot %d samples\n", sample_count);

        if (measure) {
                measure_samples(timing, sample_count);
        }
	if (quantize) {
		printf("Quantize!\n");
                quantize_samples(timing, sample_count);
	}

	rc = find_std_sector_headers(timing, sample_count, header, 11);
	printf("got %d headers\n", rc);

	if (!rc) {
		fprintf(stderr,
			"Couldn't find any standard sectors in data stream!\n"
			);
	} else {
		h = header;
		for (i = 0; i < rc; i++) {
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
	}

	free(timing);
        return 0;
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

