#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <endian.h>

#include "arm-interface.h"
#include "pru-setup.h"

void hexdump(const void *b, size_t len)
{
    int i;
    const uint8_t *buf = b;
    char str[17];
    char *c = str;

    for (i=0; i < len; i++) {
        if (i && !(i % 16)) {
            *c = '\0';
            printf("%s\n", str);
            c = str;
        }
	if (!(i % 16))
		printf("0x%04X: ", i);
        *c++ = (*buf < 128 && *buf > 32) ? *buf : '.';
        printf("%02x ", *buf++);
    }

    //if (!(i % 16))
    //    return;

    *c = '\0';
    printf("%s\n", str);
}


#define min(a,b) ((a < b) ? a : b)
#define max(a,b) ((a > b) ? a : b)

#define MASK 0x55555555 /* 0b010101010101 ... 010101 */
unsigned int decode_data(void *in_buf, int len, void *out_buf)
{
	unsigned int *input = in_buf;
	unsigned int *output = out_buf;
	unsigned int odd_bits, even_bits;
	unsigned int chksum = 0L;
	int data_size = len/2;
	int i;

	memset(out_buf, 0, data_size);
	for (i = 0; i < data_size/sizeof(int); i++) {
		odd_bits = *input;
		even_bits = *(input+(data_size/sizeof(int)));
		chksum ^= odd_bits;
		chksum ^= even_bits;

		*output = be32toh((even_bits & MASK) | ((odd_bits & MASK) << 1));
		input++;
		output++;
	}
	return (chksum & MASK);

}

void bin_dump(unsigned char val)
{
        int i;
        printf("0x%02x 0b", val);
        for (i = 0; i < 8; i++)
                printf("%d", (val & (1 << i)) ? 1 : 0 );
        printf("\n");
}

struct mfm_sector {
        int odd_info;
        int even_info;
        int odd_label[4];
        int even_label[4];
        int odd_h_chksum;
        int even_h_chksum;
        int odd_d_chksum;
        int even_d_chksum;
        unsigned char odd_data[512];
        unsigned char even_data[512];
} __attribute__((packed));

#define print_sector_info(info) do { \
	printf("0x%08x: Format magic: 0x%02x, Cylinder Number: %u, Head: %s, sector_number: %2u - until end 0x%u\n", \
                        info, \
                        (info & 0xff000000) >> 24, \
                        ((info & 0x00ff0000) >> 16) >> 1, \
                        (((info & 0x00ff0000) >> 16) & 0x1) ? "LOWER" : "UPPER", \
                        (info & 0x0000ff00) >> 8, \
                        (info & 0x000000ff)); \
} while(0)

unsigned int decode_mfm_sector(unsigned char *buf, int mfm_sector_len, uint8_t *data_buf)
{
        unsigned int info;
        //int label[4];
        unsigned int head_chksum;
        unsigned int data_chksum;
        unsigned int chksum = 0L;
        struct mfm_sector *sector = (struct mfm_sector *)buf;
	/*
        hexdump(buf, 8);

        printf("Odd: 0x%08x\n", odd);
        bin_dump(buf[0]);
        bin_dump(buf[1]);
        bin_dump(buf[2]);
        bin_dump(buf[3]);
        printf("Even: 0x%08x\n", even);
        bin_dump(buf[4]);
        bin_dump(buf[5]);
        bin_dump(buf[6]);
        bin_dump(buf[7]);

        printf("Format Odd: 0x%02x\n", buf[0]);
        bin_dump(buf[0]);
        bin_dump((buf[0] & 0x55) << 1);
        printf("Format Even: 0x%02x\n", buf[4]);
        bin_dump(buf[4]);
        bin_dump(buf[4] & 0x55);
        bin_dump((buf[4] & 0x55) | ((buf[0] & 0x55) << 1));
	*/

        info = (sector->odd_info & 0x55555555);
        info <<= 1;
        info |= (sector->even_info & 0x55555555);


        //printf("Sector info: 0x%08x\n", info); 
	//printf("Size of sector: 0x%x\n", sizeof(struct mfm_sector));
        //printf("Odd: 0x%08x | Even: 0x%08x\n", sector->odd_info, sector->even_info);
	/*
	printf("0x%08x: Format magic: 0x%02x, Cylinder Number: %u, Head: %s, sector_number: %2u - until end 0x%u\n",
                        info,
                        (info & 0xff000000) >> 24,
                        ((info & 0x00ff0000) >> 16) >> 1,
                        (((info & 0x00ff0000) >> 16) & 0x1) ? "LOWER" : "UPPER",
                        (info & 0x0000ff00) >> 8,
                        (info & 0x000000ff));
	*/


        //label[0] = ((sector->odd_label[0] & MASK) << 1) | (sector->even_label[0] & MASK);
        //label[1] = ((sector->odd_label[1] & MASK) << 1) | (sector->even_label[1] & MASK);
        //label[2] = ((sector->odd_label[2] & MASK) << 1) | (sector->even_label[2] & MASK);
        //label[3] = ((sector->odd_label[3] & MASK) << 1) | (sector->even_label[3] & MASK);
        //printf("Label: 0x%08x 0x%08x 0x%08x 0x%08x\n", label[0], label[1], label[2], label[3]);

        head_chksum = ((sector->odd_h_chksum & MASK) << 1) | (sector->even_h_chksum & MASK);
        chksum ^= sector->odd_info;
        chksum ^= sector->even_info;
        chksum &= MASK;
        chksum ^= sector->odd_label[0];
        chksum ^= sector->even_label[0];
        chksum ^= sector->odd_label[1];
        chksum ^= sector->even_label[1];
        chksum ^= sector->odd_label[2];
        chksum ^= sector->even_label[2];
        chksum ^= sector->odd_label[3];
        chksum ^= sector->even_label[3];
        chksum &= MASK;
        if (chksum != head_chksum) {
                printf("Calculated Head Chksum: 0x%08x\n", chksum);
                printf("Head Checksum: 0x%08x\n", head_chksum);
        }
        chksum = 0L;

	if (!data_buf) {
		return info;
	}

        data_chksum = ((sector->odd_d_chksum & MASK) << 1) | (sector->even_d_chksum & MASK);
	chksum = decode_data(sector->odd_data, 1024, data_buf);
	if (chksum != data_chksum) {
                printf("Calculated Data Chksum: 0x%08x\n", chksum);
                printf("Data Checksum: 0x%08x\n", data_chksum);
	}
	//hexdump(decoded_data, 512);

        return info;
}

void decode_track(unsigned char *buf, int mfm_sector_len, int mfm_sector_count)
{
        int i;
	unsigned int info;
	uint8_t sector;
	unsigned char b[512];
	unsigned char *data_buf = malloc(512 * mfm_sector_count);
	if (!data_buf) return;

        for (i=0; i<mfm_sector_count; i++) {
                info = decode_mfm_sector(buf, mfm_sector_len, b);
		sector = (info >> 8);
		printf("%d -",sector);
		memcpy(data_buf + (sector * 512), b, 512);
                buf += mfm_sector_len;
        }
	printf("\n");

	//hexdump(data_buf, 512*mfm_sector_count);
	free(data_buf);
}

static struct pru * pru;
void read_track(unsigned char * track)
{
	int i;
	unsigned char * sector = track;
	sector = track;
	for (i = 0; i < 11; i++) {
		// The firmware is hardcoded to read MFM_TRACK_LEN bytes...
		// We only read one sector!
	        pru_read_sector(pru);
		memcpy(sector, pru->shared_ram, MFM_TRACK_LEN);
		sector += MFM_TRACK_LEN;
	}
}


int init_test(int argc, char ** argv)
{
	printf("Test\n");
	return 0;
}

int init_read(int argc, char ** argv)
{
	int i;
	unsigned char * track = malloc(MFM_TRACK_LEN * 11);
	if (!track) {
		fprintf(stderr, "Failed to alloc memory!\n");
		return -1;
	}

        pru_start_motor(pru);
	pru_reset_drive(pru);
        pru_set_head_dir(pru, PRU_HEAD_INC);

	for (i=0; i<10; i++) {
		read_track(track);	
		printf("\nTrack: %d\n", i);
		decode_track(track, MFM_TRACK_LEN, 11);
		if (i < 79) pru_step_head(pru, 1);
	}
        pru_stop_motor(pru);
	free(track);
	return 0;
}

int init_read_sector(int argc, char ** argv)
{
	unsigned int info;
	uint8_t data[512];

	pru_start_motor(pru);
	pru_read_sector(pru);
	info = decode_mfm_sector(pru->shared_ram, MFM_TRACK_LEN, data);
	print_sector_info(info);
	//hexdump(pru->shared_ram + 1084, 12);

	pru_stop_motor(pru);


	return 0;
}

int init_identify(int argc, char ** argv)
{
	int block_num = 880;
	int target_cyl = block_num / 22;
	//int target_sector = block_num % 11;
	//int target_head = (block_num % 22) / 11;
	unsigned int sector_info;
	int cur_cyl;

        pru_start_motor(pru);
        pru_read_sector(pru);
	sector_info = decode_mfm_sector(pru->shared_ram, MFM_TRACK_LEN, NULL);
	cur_cyl = ((sector_info & 0x00ff0000) >> 16) >> 1;
        
        if (cur_cyl != target_cyl) {
                if (cur_cyl < target_cyl)
                        pru_set_head_dir(pru, PRU_HEAD_INC);
                else
                        pru_set_head_dir(pru, PRU_HEAD_DEC);

	        pru_step_head(pru, abs(target_cyl - cur_cyl));
        }

        printf("Head correct!\n");
        pru_stop_motor(pru);

	return 0;
}

int reset_drive(int argc, char ** argv)\
{
        pru_start_motor(pru);
	pru_reset_drive(pru);
        pru_stop_motor(pru);
        return 0;
}

typedef int (*fn_init_ptr)(int, char **);
static const struct modes {
	const char *name;
	const char *short_help;
	fn_init_ptr init;
} modes[] = {
	{ "identify", "print name of disk, and exit", init_identify },
	{ "read", "read entire disk to file", init_read },
	{ "read_sector", "read and dump single sector", init_read_sector },
	{ "test", "test the motor control, one second test", init_test },
	{ "reset", "Reset head to cylinder 0", reset_drive },
	{ NULL, NULL }
};

void usage(void)
{
	const struct modes *m = modes;
	printf(
		"usage: bb-floppy <command> [<args>]\n"
		"\n"
		"command must be one of:\n"
	);
	while(m->name) {
		printf("    %s\t%s\n", m->name, m->short_help);
		m++;
	}

}

static void int_handler(int sig)
{
	pru_exit(pru);
        printf("\n");
        exit(-1);
}

int main(int argc, char **argv)
{
	const struct modes *m = modes;

	if (argc == 1) {
		usage();
		exit(1);
	}

	while(m->name) {
		if (!strncmp(argv[1], m->name, strlen(argv[1])))
			break;
		m++;
	}

	if (m->name == NULL) {
		fprintf(stderr, "fatal: Unknown command: %s\n\n", argv[1]);
		usage();
		exit(1);
	}


	pru = pru_setup();
	if (!pru)
		exit(1);

        signal(SIGINT, int_handler);

	m->init(argc - 1, argv + 1);

	pru_exit(pru);

	exit(0);
}

#if 0
        //FILE *fp;
	int mfm_sector_len = MFM_TRACK_LEN;
        unsigned int * volatile counter;
	unsigned int * volatile read_len;
        unsigned int mfm_sector_count = 11, c = 0; // DD = 11 sectors, HD = 22
	unsigned char *track_buf, *mfm_sector_buf;
       
        track_buf = malloc(mfm_sector_len * mfm_sector_count);
	if (!track_buf) {
                fprintf(stderr, "Failed to alloc buffer!\n");
                return -1;
	}
        mfm_sector_buf = track_buf;

	/* Initialize the interrupt controller data */
        signal(SIGINT, leave);

	read_len = (unsigned int *)(ram + 4);
	read_len[0] = mfm_sector_len;


	/*
        while(1) {
	        prussdrv_pru_wait_event(PRU_EVTOUT_0);
	        prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
                if (*(unsigned short *)(ram + 2) == 0xaaaa) {
                        break;
                }
                if (*(unsigned short *)(ram + 2) == 0x0001) {
	                memcpy(mfm_sector_buf, shared_ram, mfm_sector_len);
                        mfm_sector_buf += mfm_sector_len;
                        c++;
                        if (mfm_sector_count == c) {
                                printf("Got %d mfm track(s)!\n", mfm_sector_count);
                                ram[0] = 0xff;
                        }
                }
        }
	*/


        printf("Trigger - byte: 0x%02x\n", ram[0]);
        printf("Status - byte: 0x%02x\n", ram[1]);
        printf("Server - word: 0x%04x\n", ((unsigned short *)ram)[1]);
        printf("Bit remain: 0x%02x\n", ram[100]);


        //hexdump(ram + 0x200, mfm_sector_len);
	//printf("Total: %x\n", *(unsigned int *)(ram + 4));
	//printf("Total: %x\n", *(unsigned int *)(ram + 8));

        printf("\nClean Exit!\n");
        decode_track(track_buf, mfm_sector_len, mfm_sector_count);


	//hexdump(buf, mfm_sector_len);
        /*
	fp = fopen("track.raw", "w");
	fwrite(track_buf, 1, mfm_sector_len * 12, fp);
	fclose(fp);
        */

	free(track_buf);

	return 0;
}
#endif

