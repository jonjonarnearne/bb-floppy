#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <endian.h>

#include <prussdrv.h>
#include <pruss_intc_mapping.h>

#define PRU_NUM0	0
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
#if 0
void decode_data(unsigned char *buf, int len)
{
	unsigned int *output;
	unsigned int *input = buf;
	unsigned int odd_bits, even_bits;
	unsigned int chksum;
	int data_size;
	int i;

	for (i = 0; i < len/4; i++) {
		odd_bits = *input;
	}
}
#endif

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

void bin_dump(unsigned char val)
{
        int i;
        printf("0x%02x 0b", val);
        for (i = 0; i < 8; i++)
                printf("%d", (val & (1 << i)) ? 1 : 0 );
        printf("\n");
}
void decode_mfm_sector(unsigned char *buf, int mfm_sector_len)
{
        unsigned int info;
	unsigned int *test;
	int i;
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
        printf("Odd: 0x%08x | Even: 0x%08x\n", sector->odd_info, sector->even_info);
	printf("0x%08x: Sector magic: 0x%02x, number: %u - until end 0x%u\n",
                        info,
                        (info & 0xff000000) >> 24,
                        (info & 0x0000ff00) >> 8,
                        (info & 0x000000ff));
        return;
}

void decode_track(unsigned char *buf, int mfm_sector_len, int mfm_sector_count)
{
        int i;
        for (i=0; i<mfm_sector_count; i++) {
                decode_mfm_sector(buf, mfm_sector_len);
                buf += mfm_sector_len;
        }
}

static uint8_t * volatile ram;			// 8Kb // 0x2000 // 8192
static uint8_t * volatile shared_ram;		// 12Kb	// 0x3000 // 12288
static void leave(int sig)
{
        ram[0] = 0xff;
        printf("\n");
}

int main(int argc, char **argv)
{
	int rc;

        //FILE *fp;
	int mfm_sector_len = 0x1900;
        unsigned int * volatile counter;
	unsigned int * volatile read_len;
        unsigned int mfm_sector_count = 12, c = 0;
	unsigned char *track_buf, *mfm_sector_buf;
       
        track_buf = malloc(mfm_sector_len * mfm_sector_count);
	if (!track_buf) {
                fprintf(stderr, "Failed to alloc buffer!\n");
                return -1;
	}
        mfm_sector_buf = track_buf;

	/* Initialize the interrupt controller data */
	tpruss_intc_initdata pruss_intc_initdata = PRUSS_INTC_INITDATA;

	/* Initialize PRU */
	prussdrv_init();
	printf("Starting blinker!\n");

	rc = prussdrv_open(PRU_EVTOUT_0);
	if (rc) {
		fprintf(stderr, "Failed to open pruss device\n");
		return rc;
	}

	/* Get the interrupt initialized */
	prussdrv_pruintc_init(&pruss_intc_initdata);

	rc = prussdrv_map_prumem(PRUSS0_PRU0_DATARAM, (void **)&ram);
	if (rc) {
		fprintf(stderr, "Failed to setup PRU_DRAM\n");
		return rc;
	}
	rc = prussdrv_map_prumem(PRUSS0_SHARED_DATARAM, (void **)&shared_ram);
	if (rc) {
		fprintf(stderr, "Failed to setup PRU_SHARED_RAM\n");
		return rc;
	}

        memset(ram, 0x00, 0x2000);
	memset(shared_ram, 0x00, mfm_sector_len);
        signal(SIGINT, leave);

	read_len = (unsigned int *)(ram + 4);
	read_len[0] = mfm_sector_len;

	rc = prussdrv_exec_program(PRU_NUM0, "./bb-floppy.bin");
        printf("exec returned %d\n", rc);
        if (rc) {
                fprintf(stderr, "Failed to load firmware\n");
                return rc;
        }

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
                                //mfm_sector_buf = track_buf;
                                //c = 0;
                        }
                }
        }


        printf("Trigger - byte: 0x%02x\n", ram[0]);
        printf("Status - byte: 0x%02x\n", ram[1]);
        printf("Server - word: 0x%04x\n", ((unsigned short *)ram)[1]);
        printf("Bit remain: 0x%02x\n", ram[100]);


        //hexdump(ram + 0x200, mfm_sector_len);
	//printf("Total: %x\n", *(unsigned int *)(ram + 4));
	//printf("Total: %x\n", *(unsigned int *)(ram + 8));
	prussdrv_pru_disable(PRU_NUM0);
	prussdrv_exit();

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

