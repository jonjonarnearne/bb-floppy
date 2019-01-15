#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pru-setup.h"
#include "scp.h"

typedef struct _scp_file {
        uint8_t revolutions;
        char *filename;
        FILE *fp;
} _SCP_FILE, *SCP_FILE;

#define SCP_MAGIC   "SCP"
#define SCP_VERSION  0
#define SCP_REVISION 9

#define MAN_COMMODORE           0x00
#define MAN_ATARI               0x10
#define MAN_APPLE               0x20
#define MAN_PC                  0x30
#define MAN_TANDY               0x40
#define MAN_TEXAS_INSTRUMENTS   0x50
#define MAN_ROLAND              0x60

// CBM DISK TYPES
#define DISK_C64                0x00
#define DISK_AMIGA              0x04

#define SCP_FLAG_INDEX          0x01
#define SCP_FLAG_96TPI          0x02 // 96 or 48 TPI only for 5.25"
#define SCP_FLAG_RPM360         0x04 // 300rpm or 360rpm
#define SCP_FLAG_NORMALIZED     0x08
#define SCP_FLAG_RW             0x10
#define SCP_FLAG_FOOTER         0x20

static SCP_FILE create_scp(const char *filename,
                        uint8_t revolutions)
{
        char scp_magic[] = SCP_MAGIC;
        uint8_t scp_version = SCP_VERSION << 4 | SCP_REVISION;
        uint8_t scp_disk_type = MAN_COMMODORE | DISK_AMIGA;
        uint8_t start_track = 0;
        uint8_t end_track = 80*2;
        uint8_t flags = SCP_FLAG_INDEX;
        uint8_t sample_width = 0; // 0 = 16bits
        uint8_t heads = 0; // 0 = both heads
        uint8_t sample_resolution = 0; // 0 = 25ns
        uint32_t checksum = 0;

        SCP_FILE scp = malloc(sizeof(_SCP_FILE));
        if (!scp)
                return NULL;

        scp->filename = malloc(strlen(filename) + 1);
        strcpy(scp->filename, filename);
        scp->fp = fopen(scp->filename, "wb");
        scp->revolutions = revolutions;

        fwrite(scp_magic, 3, 1, scp->fp);
        fwrite(&scp_version, 1, 1, scp->fp);
        fwrite(&scp_disk_type, 1, 1, scp->fp);
        fwrite(&revolutions, 1, 1, scp->fp);
        fwrite(&start_track, 1, 1, scp->fp);
        fwrite(&end_track, 1, 1, scp->fp);
        fwrite(&flags, 1, 1, scp->fp);
        fwrite(&sample_width, 1, 1, scp->fp);
        fwrite(&heads, 1, 1, scp->fp);
        fwrite(&sample_resolution, 1, 1, scp->fp);
        // 0x0C - 0x0F = 4 bytes!
        fwrite(&checksum, 1, sizeof(checksum), scp->fp);

        return scp;
}

static void close_scp(SCP_FILE scp)
{
        fclose(scp->fp);
}

extern struct pru * pru;

int read_scp(int argc, char ** argv)
{
        int sample_count, revolutions = 1;
        uint32_t *timing_data;
        uint32_t *index_offsets;
        enum pru_head_side head = PRU_HEAD_UPPER;
        SCP_FILE file;

        file = create_scp("scp/test.scp", revolutions);
        if (!file)
        printf("Filename: %s\n", file->filename);
        close_scp(file);
        /*
        pru_start_motor(pru);
        pru_reset_drive(pru);
        pru_set_head_dir(pru, PRU_HEAD_INC);
        pru_set_head_side(pru, head);
        sample_count = pru_read_timing(pru, &timing_data,
                                        revolutions, &offsets);

        pru_stop_motor(pru);
        */

        return 0;
}

