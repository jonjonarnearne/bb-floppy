#ifndef PRU_SETUP_H
#define PRU_SETUP_H
struct pru {
	char * volatile ram;
	char * volatile shared_ram;
};

struct pru * pru_setup(void);
void pru_exit(struct pru * pru);
void pru_wait_event(struct pru * pru);
void pru_clear_event(struct pru * pru);
void pru_send_quit(struct pru * pru);
int pru_is_done(struct pru * pru);
void pru_start_motor(struct pru * pru);
void pru_stop_motor(struct pru * pru);
#endif
