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
#define FIRMWARE_FILE   "./test-motor.bin"

int main(int argc, char **argv)
{
	int rc;

	printf("\nTest that we have motor control!\n"
	       "Make sure there is a disk in the drive,\n"
	       "and observe that the drive led is lit for 1 sec\n\n"
	       "Press RETURN to start test\n");
	getchar();

	/* Initialize the interrupt controller data */
	tpruss_intc_initdata pruss_intc_initdata = PRUSS_INTC_INITDATA;

	/* Initialize PRU */
	prussdrv_init();

	rc = prussdrv_open(PRU_EVTOUT_0);
	if (rc) {
		fprintf(stderr, "Failed to open pruss device\n");
		return rc;
	}

	/* Get the interrupt initialized */
	prussdrv_pruintc_init(&pruss_intc_initdata);

	rc = prussdrv_exec_program(PRU_NUM0, FIRMWARE_FILE);
        if (rc) {
                fprintf(stderr, "Failed to load firmware\n");
                return rc;
        }

	prussdrv_pru_wait_event(PRU_EVTOUT_0);
	prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
	prussdrv_pru_disable(PRU_NUM0);
	prussdrv_exit();

	return 0;
}

