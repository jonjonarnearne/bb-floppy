#ifndef PRU_SETUP_H
#define PRU_SETUP_H
enum pru_head_dir {
	PRU_HEAD_INC,
	PRU_HEAD_DEC,
};

struct pru {
	unsigned char * volatile ram;
	unsigned char * volatile shared_ram;
        int running;
};

struct pru * pru_setup(void);
void stop_fw(struct pru * pru);
void pru_exit(struct pru * pru);
void pru_wait_event(struct pru * pru);
void pru_clear_event(struct pru * pru);
void pru_start_motor(struct pru * pru);
void pru_stop_motor(struct pru * pru);
void pru_stop_motor(struct pru * pru);
void pru_read_sector(struct pru * pru);

void pru_set_head_dir(struct pru * pru, enum pru_head_dir dir);
void pru_step_head(struct pru * pru, uint16_t count);
void pru_reset_drive(struct pru * pru);
#endif
