#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <endian.h>
#include <time.h>

#include "pru-setup.h"
#include "scp.h"

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

#define TDH_MAGIC "TRK"

typedef struct _scp_file {
        char            *filename;
        FILE            *fp;
        uint32_t        *offset_table;
        uint8_t         revolutions;
        uint8_t         start_track;
        uint8_t         end_track;
} _SCP_FILE, *SCP_FILE;

struct scp_duration {
        uint32_t        duration;
        uint32_t        sample_count;
};

static SCP_FILE create_scp(const char *filename, uint8_t start_track,
                                uint8_t end_track, uint8_t revolutions)
{
        uint8_t header[0x10] = {0};
        uint32_t *offset_table = NULL;

        SCP_FILE scp = malloc(sizeof(_SCP_FILE));
        if (!scp) {
                fprintf(stderr, "Couldn't alloc memory for scp\n");
                return NULL;
        }

        scp->filename = malloc(strlen(filename) + 1);
        strcpy(scp->filename, filename);

        scp->revolutions = revolutions;
        scp->start_track = start_track;
        scp->end_track = end_track;

        sprintf((char *)header, SCP_MAGIC);
        header[0x3] = SCP_VERSION << 4 | SCP_REVISION;
        header[0x4] = MAN_COMMODORE | DISK_AMIGA;
        header[0x5] = revolutions;
        header[0x6] = start_track;
        header[0x7] = end_track;
        header[0x8] = SCP_FLAG_INDEX;
        header[0x9] = 0; // Sample Width
        header[0xa] = 0; // Both heads
        header[0xb] = 0; // Resolution 25ns (max)
        // 0xC - 0xF == Checksum
        memset(header + 0xc, 0x00, 4);


        offset_table = calloc(sizeof(*offset_table), end_track + 1);
        if (!offset_table) {
                fprintf(stderr, "Couldn't alloc memory for offset table\n");
                return NULL;
        }
        scp->offset_table = offset_table;
        // Mark the start for the first TrackDataHeader (TDH)
        scp->offset_table[0] = 0x10 + (sizeof(*offset_table) * (end_track+1));

        scp->fp = fopen(scp->filename, "wb");
        fwrite(header, 1, 0x10, scp->fp);
        fwrite(offset_table, end_track + 1, sizeof(*offset_table), scp->fp);

        return scp;
}

static int add_scp_track(SCP_FILE scp, uint8_t track_no, uint16_t *flux_data,
                                                struct scp_duration *durations)
{
        int i;
        uint8_t tdh[0x4] = {0};
        uint32_t *revolution_data;
        uint32_t *revolution;
        uint32_t offset;
        int sample_count = 0;
        time_t t;
        struct tm *now;
        size_t timestamp_size;
        char timestamp[20];
        assert(scp);
        assert(track_no >= 0);
        assert(track_no <= scp->end_track);
        assert(scp->offset_table[track_no]);

        sprintf((char *)tdh, TDH_MAGIC);
        tdh[0x3] = track_no;

        revolution_data = calloc(sizeof(*revolution_data),
                                        3 * scp->revolutions);
        if (!revolution_data) {
                fprintf(stderr, "Couldn't alloc buf for revolution_data!\n");
                return -1;
        }
        revolution = revolution_data;
        offset = 0x4 + (sizeof(*revolution_data) * 3 * scp->revolutions);

        for (i=0; i < scp->revolutions; i++) {
                revolution[0] = htole32(durations[i].duration);
                revolution[1] = htole32(durations[i].sample_count);
                revolution[2] = htole32(offset);
                offset += durations[i].sample_count;
                sample_count += durations[i].sample_count;
                revolution += 3;
        }


        t = time(NULL);
        now = gmtime(&t);
        timestamp_size = strftime(timestamp, 20, "%D %T", now);
        if (!timestamp_size) {
                fprintf(stderr, "Failed to get timestamp!\n");
        }

        fseek(scp->fp, scp->offset_table[track_no], SEEK_SET);
        fwrite(tdh, 1, 0x4, scp->fp);
        fwrite(revolution_data, 3 * scp->revolutions,
                sizeof(*revolution_data), scp->fp);
        fwrite(flux_data, sample_count, sizeof(*flux_data), scp->fp);
        fwrite(timestamp, timestamp_size, sizeof(*timestamp), scp->fp);

        // Setup offset_table to point to the next track (TDH)
        if (track_no < scp->end_track) {
                scp->offset_table[track_no + 1] =
                                        scp->offset_table[track_no] + 0x4;
                scp->offset_table[track_no + 1] +=
                        (sizeof(*revolution_data) * 3 * scp->revolutions);
                scp->offset_table[track_no + 1] +=
                                        sample_count * sizeof(*flux_data);
                scp->offset_table[track_no + 1] +=
                                                        timestamp_size;
                fprintf(stderr, "New offset: %x\n", scp->offset_table[track_no + 1]);
        }

        return 0;
}

static void close_scp(SCP_FILE scp)
{
        assert(scp);
        free(scp->offset_table);
        fclose(scp->fp);
}

extern struct pru * pru;

static int samples2scp(uint16_t **scp_samples, uint32_t *original_sample,
                                                int sample_count)
{
        int i, added_samples = 0;
        uint16_t *sample_ptr;

        for (i = 0; i < sample_count; i++) {
                original_sample[i] <<= 1;
                original_sample[i] /= 5;
                if (original_sample[i] <= UINT16_MAX) continue;
                uint32_t tmp = original_sample[i];
                while(tmp > UINT16_MAX) {
                        tmp -= UINT16_MAX;
                        added_samples++;
                }
        }
        *scp_samples = calloc(sizeof(**scp_samples), sample_count + added_samples);
        if (!*scp_samples) {
                fprintf(stderr, "Coulden't alloc memory for scp_samples\n");
                return -1;
        }
        sample_ptr = *scp_samples;
        for (i = 0; i < sample_count; i++) {
                uint32_t tmp = original_sample[i];
                while(tmp > UINT16_MAX) {
                        tmp -= UINT16_MAX;
                        sample_ptr++;
                }
                sample_ptr[0] = htobe16((uint16_t)tmp);
                sample_ptr++;
        }
        return sample_count + added_samples;
}

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
int read_scp(int argc, char ** argv)
{
        int sample_count;
        uint8_t start_track = 0;
        uint8_t end_track = 1;
        uint8_t revolutions = 1;
        uint32_t timing_data[4] = { 0x00000003, 0x00000400, UINT16_MAX, UINT16_MAX * 2 + 5 };
        uint32_t *index_offsets;
        uint16_t *converted_data;
        struct scp_duration duration = {
                .duration = 8000000/25, // = 200,000,000ns = 200,000us = 200ms
                .sample_count = 4
        };

        enum pru_head_side head = PRU_HEAD_UPPER;
        SCP_FILE file;

        file = create_scp("scp/test.scp", start_track, end_track, revolutions);
        if (!file)
        printf("Filename: %s\n", file->filename);

        sample_count = ARRAY_SIZE(timing_data);
        sample_count = samples2scp(&converted_data, timing_data, sample_count);
        duration.sample_count = sample_count;

        add_scp_track(file, 0, converted_data, &duration);
        close_scp(file);
        free(converted_data);
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

