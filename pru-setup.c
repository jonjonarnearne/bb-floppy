#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <prussdrv.h>
#include <pruss_intc_mapping.h>

#include "arm-interface.h"
#include "pru-setup.h"

#define PRU_NUM0	0

extern char _firmware_size[]		asm("_binary_firmware_bin_size");
extern unsigned int firmware_data[]	asm("_binary_firmware_bin_start");

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
	if (intf->command != COMMAND_START_MOTOR_ACK) {
		printf("Hm, wrong ack on start!\n");
	}
	printf("Motor started\n");
	return;
}

void pru_stop_motor(struct pru * pru)
{
	struct ARM_IF *intf = (struct ARM_IF *)pru->ram;	
        if (!pru->running) return;

	intf->command = COMMAND_STOP_MOTOR;
	prussdrv_pru_wait_event(PRU_EVTOUT_0);
	prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
	if (intf->command != COMMAND_STOP_MOTOR_ACK) {
		printf("Hm, wrong ack on stop!\n");
	}
	printf("Motor stopped\n");
	return;
}

void pru_read_sector(struct pru * pru)
{
	struct ARM_IF *intf = (struct ARM_IF *)pru->ram;	
        if (!pru->running) return;

	intf->command = COMMAND_READ_SECTOR;
        prussdrv_pru_wait_event(PRU_EVTOUT_0);
        prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
	if (intf->command != (COMMAND_READ_SECTOR & 0x7f))
                printf("Got wrong Ack: 0x%02x\n", intf->command);

	return;
}

void pru_set_head_dir(struct pru * pru, enum pru_head_dir dir)
{
	struct ARM_IF *intf = (struct ARM_IF *)pru->ram;	
        if (!pru->running) return;

	intf->command = COMMAND_SET_HEAD_DIR;
	intf->argument = (dir == PRU_HEAD_INC) ? 1 : 0;
        prussdrv_pru_wait_event(PRU_EVTOUT_0);
        prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
	if (intf->command != (COMMAND_SET_HEAD_DIR & 0x7f))
                printf("Got wrong Ack: 0x%02x\n", intf->command);

	return;
}

void pru_step_head(struct pru * pru, uint16_t count)
{
	struct ARM_IF *intf = (struct ARM_IF *)pru->ram;	
        if (!pru->running) return;

	printf("Request to step: %d times\n", count);
	intf->command = COMMAND_STEP_HEAD;
	if (count > 80) count = 80;
	intf->argument = count;
        prussdrv_pru_wait_event(PRU_EVTOUT_0);
        prussdrv_pru_clear_event(PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);
	if (intf->command != (COMMAND_STEP_HEAD & 0x7f))
                printf("Got wrong Ack: 0x%02x\n", intf->command);

	return;
}
