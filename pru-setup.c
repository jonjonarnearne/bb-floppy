#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <prussdrv.h>
#include <pruss_intc_mapping.h>

#include "pru-setup.h"

#define PRU_NUM0	0

extern char _firmware_size[]		asm("_binary_firmware_bin_size");
extern unsigned int firmware_data[]	asm("_binary_firmware_bin_start");

struct pru * pru_setup(void)
{
	int rc;
	char * volatile ram;			// 8Kb // 0x2000 // 8192
	char * volatile shared_ram;		// 12Kb	// 0x3000 // 12288

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

	pru->ram = ram;
	pru->shared_ram = shared_ram;

	rc = prussdrv_exec_code(PRU_NUM0, firmware_data, firmware_size);
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


