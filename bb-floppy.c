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

static uint8_t * volatile ram;			// 8Kb // 0x2000 // 8192
static uint8_t * volatile shared_ram;		// 12Kb	// 0x3000 // 12288
static void leave(int sig)
{
        ram[0] = 0xff;
        printf("\n");
}

#define min(a,b) ((a < b) ? a : b)
#define max(a,b) ((a > b) ? a : b)

#define MASK 0x55555555 /* 0b010101010101 ... 010101 */
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
void decode_sector(unsigned char *buf)
{
        unsigned int info;
        struct mfm_sector *sector = (struct mfm_sector *)buf;
        unsigned int odd = htobe32(((unsigned int *)buf)[0]);
        unsigned int even = htobe32(((unsigned int *)buf)[1]);
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

        info = (sector->odd_info & 0x55555555);
        info <<= 1;
        info |= (sector->even_info & 0x55555555);

        printf("Sector info: 0x%08x\n", info); 
        return;
}

int main(int argc, char **argv)
{
	int rc;

	FILE *fp;
	int track_len = 0x1900;
        unsigned int * volatile counter;
	unsigned int * volatile read_len;
        unsigned int c;
	unsigned char *buf;
       
	buf = malloc(track_len);
	if (!buf) {
                fprintf(stderr, "Failed to alloc buffer!\n");
                return -1;
	}

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
        signal(SIGINT, leave);

        counter = (unsigned int *)(ram + 4);
	read_len = (unsigned int *)shared_ram;
	read_len[0] = track_len;

	rc = prussdrv_exec_program(PRU_NUM0, "./bb-floppy.bin");
        printf("exec returned %d\n", rc);
        if (rc) {
                fprintf(stderr, "Failed to load firmware\n");
                return rc;
        }

        while(1) {
	        prussdrv_pru_wait_event(PRU_EVTOUT_0);
	        prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
                c = counter[0];
                if (*(unsigned short *)(ram + 1) == 0xaaaa) {
                        printf("%08x\n", *(unsigned int *)(ram + 8));
                        printf("%08x\n", *(unsigned int *)(ram + 12));
                        hexdump(ram + 8, 8);
                        bin_dump(ram[11]);
                        break;
                }


                if (c < 140) {
                        //printf("Group 1: %u\n", c);
                } else if (c > 168 && c < 187) {
                        //printf("Group 2: %u\n", c);
                } else if (c > 230 && c < 255) {
                        //printf("Group 3: %u\n", c);
                } else {
                        //printf("OOB: %u\n", c);
                }
                printf("%08x\n", *(unsigned int *)(ram + 8));
                printf("%08x\n", *(unsigned int *)(ram + 12));
                hexdump(ram + 8, 8);
                bin_dump(ram + 10);
        }

        printf("Magic: %x\n", ram[0]);
        printf("Magic: %x\n", ram[1]);
        printf("Magic: %x\n", ram[2]);
        printf("Magic: %x\n", ram[3]);


        //hexdump(ram + 0x200, track_len);
	memcpy(buf, ram + 0x200, track_len);
	//printf("Total: %x\n", *(unsigned int *)(ram + 4));
	//printf("Total: %x\n", *(unsigned int *)(ram + 8));
	prussdrv_pru_disable(PRU_NUM0);
	prussdrv_exit();

        printf("\nClean Exit!\n");
        decode_sector(buf);


	//hexdump(buf, track_len);
#if 0
	fp = fopen("track.raw", "w");
	fwrite(buf, 1, track_len, fp);
	fclose(fp);
#endif
	free(buf);

	return 0;
}

