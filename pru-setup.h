#ifndef PRU_SETUP_H
#define PRU_SETUP_H
struct pru {
	char * volatile ram;
	char * volatile shared_ram;
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
#endif
