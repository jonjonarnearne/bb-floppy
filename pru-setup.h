#ifndef PRU_SETUP_H
#define PRU_SETUP_H

//#define MFM_TRACK_LEN	 0x1900 //1080 //0x1900

#define LE_SYNC_WORD            0x4489
#define BE_SYNC_WORD            0x8944
#define SECTORS_PER_TRACK       11
#define TRACKS_PER_CYLINDER     2
#define CYLINDERS_PER_DISK      80

#define RAW_MFM_SECTOR_DATA_SIZE        1024
#define RAW_MFM_SECTOR_HEAD_SIZE        56
#define RAW_MFM_SECTOR_MARKER_SIZE      8
#define RAW_MFM_SECTOR_SIZE (RAW_MFM_SECTOR_MARKER_SIZE \
                           + RAW_MFM_SECTOR_HEAD_SIZE \
                           + RAW_MFM_SECTOR_DATA_SIZE)
#define RAW_MFM_TRACK_SIZE (RAW_MFM_SECTOR_SIZE * SECTORS_PER_TRACK)


enum pru_sync {
        PRU_SYNC_DEFAULT,
        PRU_SYNC_INDEX,
        PRU_SYNC_NONE,
        PRU_SYNC_CUSTOM
};
enum pru_head_dir {
	PRU_HEAD_INC,
	PRU_HEAD_DEC,
};
enum pru_head_side {
        PRU_HEAD_UPPER,
        PRU_HEAD_LOWER
};

struct pru {
	unsigned char * volatile ram;
	unsigned char * volatile shared_ram;
        int running;
};

void hexdump(const void *b, size_t len);

struct pru * pru_setup(void);
void stop_fw(struct pru * pru);
void pru_exit(struct pru * pru);
void pru_wait_event(struct pru * pru);
void pru_clear_event(struct pru * pru);
void pru_start_motor(struct pru * pru);
void pru_stop_motor(struct pru * pru);
void pru_stop_motor(struct pru * pru);

void pru_find_sync(struct pru * pru);
void pru_read_sector(struct pru * pru, void * data);
void pru_read_raw_track(struct pru * pru, void * data, uint32_t len,
                enum pru_sync sync_type, uint32_t sync_dword);
#define pru_read_track(p, d) \
        pru_read_raw_track(p, d, 64, PRU_SYNC_DEFAULT, 0)

void pru_erase_track(struct pru * pru);
void pru_write_track(struct pru * pru, void *track);

void pru_set_head_dir(struct pru * pru, enum pru_head_dir dir);
void pru_set_head_side(struct pru * pru, enum pru_head_side side);
void pru_step_head(struct pru * pru, uint16_t count);
void pru_reset_drive(struct pru * pru);
void pru_get_bit_timing(struct pru * pru, void * data);
#endif
