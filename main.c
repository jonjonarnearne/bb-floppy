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
}__attribute__((packed));

#define print_sector_info(info) do { \
	printf("0x%08x: Format magic: 0x%02x, Cylinder Number: %u, Head: %s, sector_number: %2d - until end: %2d\n", \
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

        info = (sector->odd_info & 0x55555555);
        info <<= 1;
        info |= (sector->even_info & 0x55555555);
	print_sector_info(info);


        head_chksum = ((sector->odd_h_chksum & MASK) << 1)
                        | (sector->even_h_chksum & MASK);
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
                print_sector_info(info);
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
                print_sector_info(info);
                printf("Calculated Data Chksum: 0x%08x\n", chksum);
                printf("Data Checksum: 0x%08x\n", data_chksum);
	}

        return info;
}

#define MFM_MASK 0xaaaaaaaa
// MASK = 0x55     | EVEN_BITS | DATA
// MFM_MASK = 0xaa | ODD_BITS  | CLOCK
uint32_t encode_data(void *in_buf, void *out_buf_odd, void *out_buf_even)
{
	uint32_t *input = in_buf;
	uint32_t *output_odd = out_buf_odd;
	uint32_t *output_even = out_buf_even;
	uint32_t odd_bits, even_bits;
	uint32_t chksum = 0L;
	int i;

	//memset(out_buf, 0xaa, 1024);
	for (i = 0; i < 512/sizeof(int); i++) {
		odd_bits = htobe32(*input & MFM_MASK) >> 1;
		even_bits = htobe32(*input & MASK);

		chksum ^= odd_bits;
		chksum ^= even_bits;

		*output_odd = odd_bits;
		*output_even = even_bits;

		input++;
		output_odd++;
		output_even++;
	}
	return (chksum & MASK);
}
void encode_mfm_sector(uint8_t sector_number, uint8_t sector_offset,
			uint8_t track, enum pru_head_side side, void *data,
							void *out_mfm_sector)
{
	int i;
	struct mfm_sector *mfm_sector = out_mfm_sector;
	static const uint32_t label[4] = {0};
	uint32_t chksum=0, info=0;
	info |= (0xff << 24); // Magic
	info |= ((track << 1) << 16); // Cylinder/Track number
	info |= (side << 16);	// Head 1 upper, 0 lower
	info |= (sector_number << 8);
	info |= (sector_offset);
	print_sector_info(info);

	mfm_sector->odd_info = (info & MFM_MASK) >> 1;
	mfm_sector->even_info = (info & MASK);
	chksum ^= mfm_sector->odd_info;
	chksum ^= mfm_sector->even_info;
	chksum &= MASK;
	
	for (i=0; i<4; i++) {
		mfm_sector->odd_label[i] = (label[i] & MFM_MASK) >> 1;
		mfm_sector->even_label[i] = (label[i] & MASK);
		chksum ^= mfm_sector->odd_label[i];
		chksum ^= mfm_sector->even_label[i];
	}
	chksum &= MASK;
	mfm_sector->odd_h_chksum = (chksum & MFM_MASK) >> 1;
	mfm_sector->even_h_chksum = chksum & MASK;

	chksum = encode_data(data, mfm_sector->odd_data, mfm_sector->even_data);
	mfm_sector->odd_d_chksum = (chksum & MFM_MASK) >> 1;
	mfm_sector->even_d_chksum = chksum & MASK;

	return;
}

static void usage(void);
static struct pru * pru;

/* Call this after pru_read_track, to sync up the data.
 * If we read an entire track,
 * we might have to sync to the sync byte for successive sectors
 */
static void decode_track(uint8_t * volatile p_ram, uint8_t *mfm_track, uint8_t *data_track)
{
	unsigned int info, i, e, shifts, dwords_len;
        uint8_t * volatile ram;
        uint32_t * volatile dwords;
        uint32_t mask = 0;
	uint8_t data[512];
        uint8_t *mfm_sectors = mfm_track;
	uint8_t *data_sectors = data_track;

        ram = p_ram;

        for (e = 0; e<11; e++) {
                info = decode_mfm_sector(ram, 1080, data);
                //print_sector_info(info);
		if (mfm_sectors)
			memcpy(mfm_sectors + (1080 * ((info & 0xff00) >> 8)), ram, 1080);
		if (data_sectors)
			memcpy(data_sectors + (512 * ((info & 0xff00) >> 8)), data, 512);

                if (e == 10) break;

                ram += 1080;
                dwords = (uint32_t * volatile)ram;

                // Check if we can read the sync word,
                // else we must shift the rest of ram left
                mask = 0;
                for(i=0; i<24; i++) {
                        if (dwords[1] == 0x44894489)
                                break;

                        mask <<= 0x01;
                        mask |= 0x01;

                        dwords[0] <<= 1;
                        dwords[0] |= (dwords[1] & 0x80000000) >> 31;

                        dwords[1] <<= 1;
                        dwords[1] |= (dwords[2] & 0x80000000) >> 31;

                        dwords[2] <<= 1;
                }
                if (dwords[1] != 0x44894489) {
                        // We try to correct up to 24 bits - then give up
                        printf("Couldn't sync data. Exit\n");
                        break;
                }
                if (mask) {
                        // We had to shift, now shift rest of ram
                        printf("%08x %08x\n", dwords[0], dwords[1]);
                        shifts = i;
                        printf("Shifted %d times!\n", shifts);
                        printf("%02x - MASK\n", mask);
                        printf("%08x\n", dwords[2]);
                        mask <<= (32-i);
                        printf("%02x - MASK\n", mask);
                        dwords[2] |= (dwords[3] & mask) >> (32 - i);
                        printf("%08x\n", dwords[2]);

                        dwords += 3;
                        dwords_len = (0x3000 - (1080 + 12)) / 4;
        
                        for(i=0; i<dwords_len; i++) {
                                dwords[i] <<= shifts;
                                if (i == (dwords_len-1)) break;
                                dwords[i] |= (dwords[i+1] & mask) >> (32-shifts);
                        }
                        dwords -= 1;
                }

                ram += 8; // Skip the SYNC_WORD
        }
}


int init_test(int argc, char ** argv)
{
	printf("Test\n");
	return 0;
}

int init_read(int argc, char ** argv)
{
        FILE *fp;
	int i;
	uint8_t *mfm_track, *mfm_disk, *data_track, *data_disk;
	char filename[255];
	if (argc != 3) {
		fprintf(stderr, "You must specify a filename\n");
		return -1;
	}

	mfm_disk = malloc((1080 * 11 * 2 * 80) + (512 * 11 * 2 * 80));
	if (!mfm_disk) {
		fprintf(stderr, "Failed to alloc memory!\n");
		return -1;
	}

	mfm_track = mfm_disk;
	data_disk = mfm_disk + (1080 * 11 * 2 * 80);
	data_track = data_disk;

        pru_start_motor(pru);
	pru_reset_drive(pru);
        pru_set_head_dir(pru, PRU_HEAD_INC);

	for (i=0; i<80; i++) {
		printf("\nTrack: %d\n", i);
                pru_set_head_side(pru, PRU_HEAD_UPPER);
		pru_read_track(pru);	
                decode_track(pru->shared_ram, mfm_track, data_track);
		mfm_track += (1080 * 11);
		data_track += (512 * 11);
                pru_set_head_side(pru, PRU_HEAD_LOWER);
		pru_read_track(pru);
                decode_track(pru->shared_ram, mfm_track, data_track);
		mfm_track += (1080 * 11);
		data_track += (512 * 11);
		if (i < 79) 
			pru_step_head(pru, 1);
	}
        pru_stop_motor(pru);

	snprintf(filename, 255, "%s.mfm", argv[2]);
	fp = fopen(filename, "w");
	fwrite(mfm_disk, 1080, 11 * 2 * 80, fp);
	fclose(fp);
	snprintf(filename, 255, "%s.adf", argv[2]);
	fp = fopen(filename, "w");
	fwrite(data_disk, 512, 11 * 2 * 80, fp);
	fclose(fp);

	free(mfm_disk);
	return 0;
}

int init_read_track(int argc, char ** argv)
{
        uint8_t *mfm_track = malloc(1080 * 11);
        uint8_t *data_track = malloc(512 * 11);
	pru_start_motor(pru);
	pru_read_track(pru);
	pru_stop_motor(pru);

        hexdump(pru->shared_ram, 8);
        decode_track(pru->shared_ram, mfm_track, data_track);
        free(mfm_track);
        free(data_track);

	return 0;
}

int init_read_sector(int argc, char ** argv)
{
        int sector_info;
	uint8_t data[512] = {0};
	memset(pru->shared_ram, 0x00, 0x3000);

	pru_start_motor(pru);
	pru_read_sector(pru);
	pru_stop_motor(pru);

        hexdump(pru->shared_ram, 16);

	sector_info = decode_mfm_sector(pru->shared_ram, MFM_TRACK_LEN, data);
	print_sector_info(sector_info);

	return 0;
}

#define BYTE_TO_BIN(byte) \
        (byte & 0x80 ? '1' : '0'), \
        (byte & 0x40 ? '1' : '0'), \
        (byte & 0x20 ? '1' : '0'), \
        (byte & 0x10 ? '1' : '0'), \
        (byte & 0x08 ? '1' : '0'), \
        (byte & 0x04 ? '1' : '0'), \
        (byte & 0x02 ? '1' : '0'), \
        (byte & 0x01 ? '1' : '0')
#if 0
        printf("0b%c%c%c%c%c%c%c%c\n", BYTE_TO_BIN(sector[dword] & 0xff));
        printf("0b%c%c%c%c%c%c%c%c\n", BYTE_TO_BIN((sector[dword] & 0xff00) >> 8));
        printf("0b%c%c%c%c%c%c%c%c\n", BYTE_TO_BIN((sector[dword] & 0xff0000) >> 16));
        printf("0b%c%c%c%c%c%c%c%c\n", BYTE_TO_BIN((sector[dword] & 0xff000000) >> 24));
#endif

static void set_mfm_clock(void * data, size_t len)
{
	unsigned int *sector = data;
        int dword, bit;
        unsigned int this_bit, last_bit;

        this_bit = sector[1] & 1;

        // The first two dwords are the sector markers
        for(dword = 2; dword < (len / 4); dword++) {
                for(bit = 31; bit >= 1; bit -= 2) {
                        last_bit = this_bit;
                        this_bit = (sector[dword] & (1 << (bit - 1)));
                        if (!(last_bit | this_bit))
                                sector[dword] |= (1 << bit);

                }
        }
}

#define RAW_MFM_SECTOR_DATA_SIZE 1024
#define RAW_MFM_SECTOR_HEAD_SIZE 56
#define RAW_MFM_SECTOR_MARKER_SIZE 8
#define RAW_MFM_SECTOR_SIZE (RAW_MFM_SECTOR_MARKER_SIZE \
                           + RAW_MFM_SECTOR_HEAD_SIZE \
                           + RAW_MFM_SECTOR_DATA_SIZE)

int init_erase_track(int argc, char ** argv)
{
	pru_erase_track(pru);
        return 0;
}

int init_write_track(int argc, char ** argv)
{
	int i;
	uint8_t data[512] = {0};
	uint32_t *sector;
	unsigned char *track = malloc(0x3000);
	if (!track) return -1;

	memset(track, 0xaa, 0x3000);
	for(i=0; i<11; i++) {
		sector = (uint32_t *)(track + (RAW_MFM_SECTOR_SIZE * i));
		sector[1] = 0x44894489;
		encode_mfm_sector(i, (11-i), 0, PRU_HEAD_UPPER, data, &sector[2]);
		set_mfm_clock(sector, RAW_MFM_SECTOR_SIZE);
	}

	for (i=0; i<11; i++) {
		hexdump(track + (i * RAW_MFM_SECTOR_SIZE), 16);
		decode_mfm_sector(track + 8 + (RAW_MFM_SECTOR_SIZE * i), 1080, data);
	}

	memcpy(pru->shared_ram, track, 0x3000);
	free(track);

	pru_write_track(pru, track);
	printf("Done!\n");

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

int find_sync(int argc, char ** argv)
{
        pru_start_motor(pru);
	pru_find_sync(pru);
        pru_stop_motor(pru);
        return 0;
}

int reset_drive(int argc, char ** argv)
{
        pru_start_motor(pru);
	pru_reset_drive(pru);
        pru_stop_motor(pru);
        return 0;
}

int write_raw_mfm(int argc, char ** argv)
{
	FILE *fp;
	size_t items, count = 0;

	unsigned int dword;
	unsigned int *dwords = malloc(RAW_MFM_SECTOR_SIZE);
	if (!dwords)
		exit(-1);

	printf("argc: %d\n", argc);
	if (argc != 3) {
		usage();
		printf("You must give a filename to a mfm file\n");
		return -1;	
	}

	dwords[0] = 0xaaaaaaaa;
	dwords[1] = htobe32(0x44894489);

	count = 2;
	fp = fopen(argv[2], "r");
	while(1) {
		items = fread(&dword, sizeof(dword), 1, fp);
		if (!items) break;
		dwords[count] = htobe32(dword);
		count++;
		if (count == RAW_MFM_SECTOR_SIZE/4) break;
	}
	fclose(fp);

	printf("Done: read: %d\n", count * 4);
	hexdump(dwords, RAW_MFM_SECTOR_SIZE);
	free(dwords);
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
	{ "read_track", "read and dump single track", init_read_track },
	{ "read_sector", "try to read a single sector", init_read_sector },
	{ "write_track", "write a single track", init_write_track },
	{ "erase_track", "erase a single track", init_erase_track },
	{ "test", "test the motor control, one second test", init_test },
	{ "find_sync", "See if we find any sync marker", find_sync },
	{ "reset", "Reset head to cylinder 0", reset_drive },
	{ "write_raw", "Write raw mfm file to drive", write_raw_mfm },
	{ NULL, NULL }
};

static void usage(void)
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

	m->init(argc, argv);

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


