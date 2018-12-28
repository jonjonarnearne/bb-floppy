#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <prussdrv.h>
#include <pruss_intc_mapping.h>

#include "arm-interface.h"
#include "pru-setup.h"
#include "list.h"

#define PRU_NUM0	0

extern char _firmware_size[]		asm("_binary_firmware_bin_size");
extern unsigned int firmware_data[]	asm("_binary_firmware_bin_start");

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

struct pru * pru_setup(void)
{
	int rc;
	unsigned char * volatile ram;			// 8Kb // 0x2000 // 8192
	unsigned char * volatile shared_ram;		// 12Kb	// 0x3000 // 12288

	int firmware_size = (int)(void *)_firmware_size;

	struct pru * pru;

	tpruss_intc_initdata pruss_intc_initdata = PRUSS_INTC_INITDATA;

	/* Initialize PRU */
	prussdrv_init();

	rc = prussdrv_open(PRU_EVTOUT_0);
	if (rc) {
		fprintf(stderr, "Failed to open pruss device\n");
		return NULL;
	}

	/* Get the interrupt initialized */
	prussdrv_pruintc_init(&pruss_intc_initdata);

	rc = prussdrv_map_prumem(PRUSS0_PRU0_DATARAM, (void **)&ram);
	if (rc) {
		fprintf(stderr, "Failed to setup PRU_DRAM\n");
		return NULL;
	}
	rc = prussdrv_map_prumem(PRUSS0_SHARED_DATARAM, (void **)&shared_ram);
	if (rc) {
		fprintf(stderr, "Failed to setup PRU_SHARED_RAM\n");
		return NULL;
	}

        memset(ram, 0x00, 0x2000);
	memset(shared_ram, 0x00, 0x3000);

	pru = malloc(sizeof(*pru));
	if (!pru) {
		prussdrv_exit();
		return NULL;
	}

        pru->running = 0;
	pru->ram = ram;
	pru->shared_ram = shared_ram;

	rc = prussdrv_exec_code(PRU_NUM0, firmware_data, firmware_size);
        if (rc) {
                fprintf(stderr, "Failed to load firmware\n");
		free(pru);
		prussdrv_exit();
                return NULL;
        }
	prussdrv_pru_wait_event(PRU_EVTOUT_0);
	prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);

        pru->running = 1;

	// The firmware is now waiting for command!

	return pru;
}

void stop_fw(struct pru * pru)
{
	volatile struct ARM_IF *intf = (struct ARM_IF *)pru->ram;	
        if (!pru->running) return;

        pru->running = 0;
	intf->command = COMMAND_QUIT;

	prussdrv_pru_wait_event(PRU_EVTOUT_0);
	prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);

	if (intf->command != (COMMAND_QUIT & 0x7f))
                printf("QUIT wrong Ack: 0x%02x\n", intf->command);

	return;
}

void pru_exit(struct pru * pru)
{
        if (pru->running)
                stop_fw(pru);

	prussdrv_pru_disable(PRU_NUM0);
	prussdrv_exit();
        free(pru);
}

void pru_wait_event(struct pru * pru)
{
        if (!pru->running) return;
	prussdrv_pru_wait_event(PRU_EVTOUT_0);
}
void pru_clear_event(struct pru * pru)
{
        if (!pru->running) return;
	prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
}


void pru_start_motor(struct pru * pru)
{
	volatile struct ARM_IF *intf = (struct ARM_IF *)pru->ram;	
        if (!pru->running) return;

	intf->command = COMMAND_START_MOTOR;
	prussdrv_pru_wait_event(PRU_EVTOUT_0);
	prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
	if (intf->command != (COMMAND_START_MOTOR & 0x7f))
		printf("Hm, wrong ack on start!\n");

	return;
}

void pru_stop_motor(struct pru * pru)
{
	struct ARM_IF *intf = (struct ARM_IF *)pru->ram;	
        if (!pru->running) return;

	intf->command = COMMAND_STOP_MOTOR;
	prussdrv_pru_wait_event(PRU_EVTOUT_0);
	prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
	if (intf->command != (COMMAND_STOP_MOTOR & 0x7f))
		printf("Hm, wrong ack on stop!\n");

	return;
}

void pru_find_sync(struct pru * pru)
{
	struct ARM_IF *intf = (struct ARM_IF *)pru->ram;	
        if (!pru->running) return;

        // IMPORTANT: SET ARGUMENT BEFORE WE SET THE COMMAND!
        // The argument is number of dwords
	intf->command = COMMAND_FIND_SYNC;
        prussdrv_pru_wait_event(PRU_EVTOUT_0);
        prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
	if (intf->command != (COMMAND_FIND_SYNC & 0x7f))
                printf("Got wrong Ack: 0x%02x\n", intf->command);

	return;
}

void pru_read_sector(struct pru * pru, void * data)
{
        int i;
        unsigned int *dest = data;
        unsigned int * volatile source = (unsigned int * volatile)pru->shared_ram;

	struct ARM_IF *intf = (struct ARM_IF *)pru->ram;	
        if (!pru->running) return;

        // IMPORTANT: SET ARGUMENT BEFORE WE SET THE COMMAND!
        // The argument is number of dwords
        memset(pru->shared_ram, 0xaa, 0x3000);

	intf->argument = (RAW_MFM_SECTOR_SIZE + 16) / 4;
	intf->command = COMMAND_READ_SECTOR;
        prussdrv_pru_wait_event(PRU_EVTOUT_0);
        prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
	if (intf->command != (COMMAND_READ_SECTOR & 0x7f))
                printf("Got wrong Ack: 0x%02x\n", intf->command);

        for(i=0; i < 0x3000/sizeof(*dest); i++) {
                dest[i] = htobe32(source[i]);
        }

	return;
}

void pru_read_raw_track(struct pru * pru, void * data, uint32_t len,
                enum pru_sync sync_type, uint32_t sync_dword)
{
        int i;
        unsigned char mul = 0;
        unsigned int *dest = data;
        unsigned int * volatile source =
                                (unsigned int * volatile)pru->shared_ram;

	struct ARM_IF *intf = (struct ARM_IF *)pru->ram;	
        if (!pru->running) return;

        // IMPORTANT: SET ARGUMENT BEFORE WE SET THE COMMAND!
        // The argument is number of dwords
        // 16 eq. just read the head. Wait for correct sector,
        // then read entire track!
        memset(pru->shared_ram, 0xff, 0x3000);

        // Lemmings sync word BE
        //ldi  find_sync.sync_word.w0, #0xaaaa
        //ldi  find_sync.sync_word.w2, #0x912a
        switch(sync_type) {
        case PRU_SYNC_DEFAULT:
                intf->sync_word = LE_SYNC_WORD | (LE_SYNC_WORD << 16);
                break;
        case PRU_SYNC_NONE:
                intf->sync_word = 0x00000000;
                break;
        case PRU_SYNC_CUSTOM:
                intf->sync_word = sync_dword;
                break;
        default:
                fprintf(stderr, "fatal: Sync type not implemented!\n");
                return;
        }
        if (len == 64) {
	        intf->argument = 16;
                len = 0x3000;
        } else {
	        intf->argument = len/4;
        }
	intf->command = COMMAND_READ_SECTOR;

        while(1) {
                prussdrv_pru_wait_event(PRU_EVTOUT_0);
                prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);

                // We read 0x1000 bytes at a time, from the 0x3000byte buffer
                // jumping back and forth in sync with the PRU
	        if (intf->command == COMMAND_READ_SECTOR) {
                        for(i=0; i < 0x1000/sizeof(*dest); i++) {
                                dest[i] = htobe32(source[i]);
                        }
                        dest += 0x1000/sizeof(*dest);
                        len -= 0x1000;

                        if (++mul & 0x1)
                                source += 0x1000/sizeof(*dest);
                        else
                                source -= 0x1000/sizeof(*dest);

                } else if (intf->command == (COMMAND_READ_SECTOR & 0x7f)) {
                        break;
                } else {
                        printf("Got wrong Ack: 0x%02x\n", intf->command);
                        break;
                }
        }
        // Read the rest of the data out of the buffer if any!
        for(i=0; i < len/sizeof(*dest); i++) {
                dest[i] = htobe32(source[i]);
        }

        //for(i=0; i < RAW_MFM_TRACK_SIZE/sizeof(*dest); i++) {
        //for(i=0; i < 0x3000/sizeof(*dest); i++) {
        //        dest[i] = htobe32(source[i]);
        //}

	return;
}

int pru_read_bit_timing(struct pru * pru, uint16_t ** data)
{
        // Read from drive for 240.000.us!
        // 240.000us of 0b10101010 = 60.000 samples (0xea60)
        int init_samp_count, sample_count = 0x186a0; //0xea60;

        uint8_t mul = 0;
        uint16_t *dest;
        uint16_t * volatile source = (uint16_t * volatile)pru->shared_ram;

	struct ARM_IF *intf = (struct ARM_IF *)pru->ram;	
        if (!pru->running)
		return 0;

	*data = NULL;
        dest = malloc(sample_count * sizeof(*dest));
        if (!dest)
		return 0;

	init_samp_count = sample_count;
	*data = dest;
        memset((unsigned char *)dest, 0x00, sample_count * sizeof(*dest));

        memset(pru->shared_ram, 0x00, 0x3000);
	intf->command = COMMAND_READ_BIT_TIMING;

        while(1) {
                prussdrv_pru_wait_event(PRU_EVTOUT_0);
                prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);

                // We read 0x1000 bytes at a time, from the 0x3000byte buffer
                // jumping back and forth in sync with the PRU
	        if (intf->command == COMMAND_READ_BIT_TIMING) {
                        memcpy(dest, source, 0x1000);
                        dest += 0x1000/sizeof(*dest);
                        sample_count -= 0x1000/sizeof(*dest);

                        if (++mul & 0x1)
                                source += 0x1000/sizeof(*dest);
                        else
                                source -= 0x1000/sizeof(*dest);

                } else if (intf->command == (COMMAND_READ_BIT_TIMING & 0x7f)) {
                        break;
                } else {
                        printf("Got wrong Ack: 0x%02x\n", intf->command);
                        break;
                }
        }
        if (sample_count > 0) 
                memcpy(dest, source, 0x1000);

        //printf("Sampled %d\n", intf->read_count);
        //printf("Sampled %d ns\n", intf->read_count * 30);
        //printf("Sampled %d us\n", intf->read_count * 30 / 1000);

	return (init_samp_count - intf->sync_word);
}

int pru_write_bit_timing(struct pru * pru, uint16_t *source,
                                                int sample_count)
{
        uint8_t mul = 0;
        uint16_t * volatile dest = (uint16_t * volatile)pru->shared_ram;
        int copy_size;

	volatile struct ARM_IF *intf = (volatile struct ARM_IF *)pru->ram;	
        if (!pru->running) return sample_count;

        intf->read_count = sample_count;
        printf("Sample count: %d, bytes: %d\n", sample_count,
                                        sample_count * sizeof(*source));

        memcpy(dest, source, 0x2000);
        source          += 0x2000/sizeof(*source);
        sample_count    -= 0x2000/sizeof(*source);

	intf->command = COMMAND_WRITE_BIT_TIMING;

        while(1) {
                // We spin here until PRU has consumed the first 0x1000 bytes.
                prussdrv_pru_wait_event(PRU_EVTOUT_0);
                prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);

                if (intf->command == COMMAND_WRITE_BIT_TIMING) {
                        // Replace one part of the buffer
                        copy_size = (sample_count * sizeof(*source) > 0x1000)
                                ? 0x1000 : sample_count * sizeof(*source);

                        if (copy_size <= 0) {
                                fprintf(stderr,
                        "fatal -- pru request samples on empty buffer!\n"
                                "Buffer: %d\n", copy_size);
                                continue;
                        }

                        memcpy(dest, source, copy_size);
                        source += copy_size/sizeof(*source);
                        sample_count -= copy_size/sizeof(*source);
                        printf("Adding %d\n", copy_size);

                        if (++mul & 0x1) {
                                dest += 0x1000/sizeof(*dest);
                        } else {
                                dest -= 0x1000/sizeof(*dest);
                        }
                        
                } else if (intf->command ==
                                (COMMAND_WRITE_BIT_TIMING & 0x7f)) {
                        printf("Done!\n");
                        break;
                } else {
                        printf("Got wrong Ack: 0x%02x\n", intf->command);
                        break;
                }
        }

        return intf->read_count;
}

void pru_erase_track(struct pru * pru)
{
	struct ARM_IF *intf = (struct ARM_IF *)pru->ram;	
        if (!pru->running) return;

	intf->command = COMMAND_ERASE_TRACK;
        prussdrv_pru_wait_event(PRU_EVTOUT_0);
        prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
	if (intf->command != (COMMAND_ERASE_TRACK & 0x7f))
                printf("Got wrong Ack: 0x%02x\n", intf->command);
}

void pru_write_track(struct pru * pru, void * data)
{
        int i;
        unsigned int *source = data;
        unsigned int * volatile dest = (unsigned int * volatile)pru->shared_ram;

	struct ARM_IF *intf = (struct ARM_IF *)pru->ram;	
        if (!pru->running) return;

        memset(pru->shared_ram, 0xaa, 0x3000);
        for(i=0; i < RAW_MFM_TRACK_SIZE/sizeof(*dest); i++) {
                dest[i] = be32toh(source[i]);
        }

	intf->command = COMMAND_WRITE_TRACK;
        prussdrv_pru_wait_event(PRU_EVTOUT_0);
        prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
	if (intf->command != (COMMAND_WRITE_TRACK & 0x7f))
                printf("Got wrong Ack: 0x%02x\n", intf->command);
}

void pru_set_head_dir(struct pru * pru, enum pru_head_dir dir)
{
	struct ARM_IF *intf = (struct ARM_IF *)pru->ram;	
        if (!pru->running) return;

        // IMPORTANT: SET ARGUMENT BEFORE WE SET THE COMMAND!
	intf->argument = (dir == PRU_HEAD_INC) ? 1 : 0;
	intf->command = COMMAND_SET_HEAD_DIR;
        prussdrv_pru_wait_event(PRU_EVTOUT_0);
        prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
	if (intf->command != (COMMAND_SET_HEAD_DIR & 0x7f))
                printf("Got wrong Ack: 0x%02x\n", intf->command);

	return;
}

void pru_set_head_side(struct pru * pru, enum pru_head_side side)
{
	struct ARM_IF *intf = (struct ARM_IF *)pru->ram;	
        if (!pru->running) return;

        // IMPORTANT: SET ARGUMENT BEFORE WE SET THE COMMAND!
	intf->argument = (side == PRU_HEAD_UPPER) ? 1 : 0;
	intf->command = COMMAND_SET_HEAD_SIDE;
        prussdrv_pru_wait_event(PRU_EVTOUT_0);
        prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
	if (intf->command != (COMMAND_SET_HEAD_SIDE & 0x7f))
                printf("Got wrong Ack: 0x%02x\n", intf->command);

	return;
}

void pru_step_head(struct pru * pru, uint16_t count)
{
	struct ARM_IF *intf = (struct ARM_IF *)pru->ram;	
        if (!pru->running) return;

	if (count > 80) count = 80;
        // IMPORTANT: SET ARGUMENT BEFORE WE SET THE COMMAND!
	intf->argument = count;
	intf->command = COMMAND_STEP_HEAD;
        prussdrv_pru_wait_event(PRU_EVTOUT_0);
        prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
	if (intf->command != (COMMAND_STEP_HEAD & 0x7f))
                printf("Got wrong Ack: 0x%02x\n", intf->command);

	return;
}

void pru_reset_drive(struct pru * pru)
{
	struct ARM_IF *intf = (struct ARM_IF *)pru->ram;	
        if (!pru->running) return;

	intf->command = COMMAND_RESET_DRIVE;
        prussdrv_pru_wait_event(PRU_EVTOUT_0);
        prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
	if (intf->command != (COMMAND_RESET_DRIVE & 0x7f))
                printf("Got wrong Ack: 0x%02x\n", intf->command);

	return;
}

int pru_test_track_0(struct pru * pru)
{
	struct ARM_IF *intf = (struct ARM_IF *)pru->ram;	
        if (!pru->running) return -1;

	intf->command = COMMAND_TEST_TRACK_0;
        prussdrv_pru_wait_event(PRU_EVTOUT_0);
        prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
	if (intf->command != (COMMAND_TEST_TRACK_0 & 0x7f))
                printf("Got wrong Ack: 0x%02x\n", intf->command);

	return intf->argument;
}

int pru_write_timing(struct pru * pru, uint16_t *source,
                                                int sample_count)
{
        uint8_t mul = 0;
        uint16_t * volatile dest = (uint16_t * volatile)pru->shared_ram;
        int copy_size;

	volatile struct ARM_IF *intf = (volatile struct ARM_IF *)pru->ram;	
        if (!pru->running) return sample_count;

        intf->read_count = sample_count;

        copy_size = (sample_count * sizeof(*source) > 0x1000) ? 0x1000 : sample_count * sizeof(*source);
        memcpy(dest, source, copy_size);
        source          += copy_size/sizeof(*source);
        sample_count    -= copy_size/sizeof(*source);

        mul = 1;
        dest += 0x1000/sizeof(*dest);

	intf->command = COMMAND_WRITE_TIMING;

        while(1) {
                // We spin here until PRU has consumed the first 0x1000 bytes.
                prussdrv_pru_wait_event(PRU_EVTOUT_0);
                prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);

                if (intf->command == COMMAND_WRITE_TIMING) {
#if 0
                        printf("INTERRUPT - sample_count: %d OFFSET: 0x%04x (%d)\n",
                                        *(uint32_t  *)(pru->ram + 0x80),
                                        *(uint16_t  *)(pru->ram + 0x90),
                                        *(uint16_t  *)(pru->ram + 0x90));
#endif
                        // Replace one part of the buffer
                        copy_size = (sample_count * sizeof(*source) > 0x1000)
                                ? 0x1000 : sample_count * sizeof(*source);

                        if (copy_size <= 0) {
                                fprintf(stderr,
                                "fatal -- pru request samples on empty buffer!"
                                                                        "\n");
                                return sample_count;
                        }

                        memcpy(dest, source, copy_size);
                        source += copy_size/sizeof(*source);
                        sample_count -= copy_size/sizeof(*source);

                        if (++mul & 0x1) {
                                dest += 0x1000/sizeof(*dest);
                        } else {
                                dest -= 0x1000/sizeof(*dest);
                        }
                        
                } else if (intf->command ==
                                (COMMAND_WRITE_TIMING & 0x7f)) {
                        break;
                } else {
                        printf("Got wrong Ack: 0x%02x\n", intf->command);
                        break;
                }
        }

        return intf->read_count;
}

/* Read the timing of each bit from current track on floppy.
 * The read will start from INDEX, and read <revolutions> revolutions.
 * The function will return the number of samples read,
 * and the <data> pointer will contain the samples.
 * If <rev_offsets> is not NULL, it will be pointed to an array
 * of offsets into data for the start of each revolution.
 */
int pru_read_timing(struct pru * pru, uint16_t ** data,
                uint8_t revolutions, uint32_t ** rev_offsets)
{
        uint16_t * volatile source = (uint16_t * volatile)pru->shared_ram;

        int rc, sample_count = 0;
        struct bb_list *buffer_list = NULL;
        uint8_t mul = 0;

	struct ARM_IF *intf = (struct ARM_IF *)pru->ram;	
        if (!data) {
                fprintf(stderr, "fatal -- data is NULL\n");
                return 0;
        }

	*data = NULL;

        if (!pru->running)
		return 0;

        rc = bb_list_new(&buffer_list);
        if (!rc) {
                fprintf(stderr,
                        "Couldn't allocate memory for data list buffer!\n");
                return 0;
        }

        memset(pru->shared_ram, 0x00, 0x3000);
        intf->argument = revolutions;
	intf->command = COMMAND_READ_TIMING;

        while(1) {
                prussdrv_pru_wait_event(PRU_EVTOUT_0);
                prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);

                // We read 0x1000 bytes at a time,
                // from the first 0x2000 bytes of the 0x3000 byte buffer
                // jumping back and forth in sync with the PRU
	        if (intf->command == COMMAND_READ_TIMING) {
                        rc = bb_list_append(buffer_list, source,
                                                0x1000/sizeof(*source));
                        if (!rc) {
                                fprintf(stderr,
                                "fatal -- Couldn't copy data to buffer!\n");
                                continue;
                        }
                        sample_count += 0x1000/sizeof(*source);

                        if (++mul & 0x1)
                                source += 0x1000/sizeof(*source);
                        else
                                source -= 0x1000/sizeof(*source);

                } else if (intf->command == (COMMAND_READ_TIMING & 0x7f)) {
			// The drive is done reading
                        break;
                } else {
                        printf("Got wrong Ack: 0x%02x\n", intf->command);
                        break;
                }
        }
        rc = bb_list_append(buffer_list, source,
                                        intf->read_count - sample_count);
        if (!rc)
                fprintf(stderr, "fatal -- Couldn't copy data to buffer!\n");

        sample_count += intf->read_count - sample_count;
        if (sample_count != intf->read_count) 
                fprintf(stderr, "ASSERTION FAILED!!!\n");

        *data = malloc(intf->read_count * sizeof(**data));
        if (!*data) {
                fprintf(stderr,
                        "Couldn't allocate memory for data list buffer!\n");
                return 0;
        }

        rc = bb_list_flatten(buffer_list, *data, intf->read_count);
        if (rc) {
                fprintf(stderr, "Another assertion failed!\n");
        }
        bb_list_free(buffer_list);
        buffer_list = NULL;

        if (!rev_offsets)
                return intf->read_count;

        *rev_offsets = malloc(0x1000);
        if (!*rev_offsets) {
                fprintf(stderr,
                        "Couldn't allocate memory for revolution offsets!\n");
                free(*data);
                *data = NULL;
                return 0;
        } 
        memcpy(*rev_offsets, pru->shared_ram + 0x2000, 0x1000);

	return intf->read_count;
}
