#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>

#include <prussdrv.h>
#include <pruss_intc_mapping.h>

#include "pru-setup.h"

#define PRU_NUM0	0

extern char firmware_data[]		asm("_binary_motor_bin_start");
extern char firmware_data_size[]	asm("_binary_motor_bin_size");
extern char firmware_data_end[]		asm("_binary_motor_bin_end");

struct pru * pru_setup(void)
{
	int rc;
	char * volatile ram;			// 8Kb // 0x2000 // 8192
	char * volatile shared_ram;		// 12Kb	// 0x3000 // 12288

#if 0
	static char path_buf[512];
	char firmware[512];
#endif

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

	pru->ram = ram;
	pru->shared_ram = shared_ram;

	// The firmware must be in the same folder as the executable!
#if 0
	readlink("/proc/self/exe", path_buf, 512);
	snprintf(firmware, 512, "%s/%s", dirname(path_buf), "tests/test-motor.bin");
	printf("Firmware data size: %d\n", (size_t)((void *)firmware_data_size));
	printf("4 bytes : 0x%02x 0x%02x 0x%02x 0x%02x\n",
			firmware_data[0],
			firmware_data[1],
			firmware_data[2],
			firmware_data[3]);
	rc = prussdrv_exec_program(PRU_NUM0, firmware);
#endif
	rc = prussdrv_exec_code(PRU_NUM0, (const unsigned int *)firmware_data,
			(size_t)((void *)firmware_data_size));
        if (rc) {
                fprintf(stderr, "Failed to load firmware\n");
		free(pru);
		prussdrv_exit();
                return NULL;
        }

	return pru;
}

void pru_exit(struct pru * pru)
{
	prussdrv_pru_disable(PRU_NUM0);
	prussdrv_exit();
}

void pru_wait_event(struct pru * pru)
{
	(void)pru;
	prussdrv_pru_wait_event(PRU_EVTOUT_0);
}
void pru_clear_event(struct pru * pru)
{
	(void)pru;
	prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
}


