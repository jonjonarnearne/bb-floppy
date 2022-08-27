#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <endian.h>
#include <time.h>
#include <unistd.h>
#include <ncurses.h>

#include "pru-setup.h"
#include "flux_data.h"
#include "read_flux.h"

void hexdump(const void *b, size_t len);
extern struct pru * pru;

struct sector_samples {
        uint32_t *samples; // Pointer to first sample of sync marker (0x4489)
        int samples_count;
        int sector_number;
};
struct track_samples {
        uint32_t *samples; // Raw sample data, must be free'd!
        int sample_count;
        struct sector_samples sectors[11];
};

struct amiga_sector {
        uint32_t header_info;
        uint32_t header_sector_label[4];
        uint32_t *data;
        bool header_checksum_ok;
        bool data_checksum_ok;
};

static bool find_sync_marker(struct track_samples *track, size_t *index);
static uint8_t *samples_to_bitsream(struct track_samples *track, size_t index, size_t *byte_count);
static int parse_amiga_mfm_sector(const uint8_t *bitstream, size_t byte_count,
                                        struct amiga_sector *parsed_sector);

int read_flux(int argc, char ** argv)
{
        uint8_t revolutions = 1;
        uint32_t *index_offsets;

        initscr();
        start_color();
        raw();
        keypad(stdscr, TRUE);
        noecho();

        init_pair(1, COLOR_BLACK, COLOR_WHITE); // Inverted for statusbar
        init_pair(2, COLOR_GREEN, COLOR_GREEN); // Good sector.
        init_pair(3, COLOR_CYAN, COLOR_CYAN); // Bad header
        init_pair(4, COLOR_YELLOW, COLOR_YELLOW); // Bad data
        init_pair(5, COLOR_RED, COLOR_RED); // Both bad

        int row, col;
        getmaxyx(stdscr,row,col);

        WINDOW *log_window = newwin(row - 1 /* height */, 40/* width */,
                                        0 /* start row */, col - 40 /* start col */);
        box(log_window, 0, 0);
        wrefresh(log_window);

        WINDOW *sector_window = newwin(row - 1 /* height */, col - 41 /* width */,
                                       0 /* start row */, 0 /* start col */);

        box(sector_window, 0, 0);
        wrefresh(sector_window);

        WINDOW *status_bar = newwin(1 /* height */, col /* width */,
                                        row - 1 /* start row */, 0 /* start col */);
        wattron(status_bar, A_REVERSE);
        wbkgd(status_bar, COLOR_PAIR(1));
        wrefresh(status_bar);


        pru_start_motor(pru);
        pru_reset_drive(pru);


        pru_set_head_dir(pru, PRU_HEAD_INC);

        struct track_samples track = {0};


        for (unsigned int i = 0; i < 3; i++) {
                pru_set_head_side(pru, i & 1 ? PRU_HEAD_LOWER : PRU_HEAD_UPPER);

                werase(status_bar);
                mvwprintw(status_bar, 0, 10, "Read track: %d, head: %d", i >> 1, i & 1);
                wrefresh(status_bar);

                werase(log_window);
                box(log_window, 0, 0);
                wrefresh(log_window);

                track.sample_count = pru_read_timing(pru, &track.samples, revolutions, &index_offsets);
                size_t index = 0;
                struct amiga_sector sector;

                for (unsigned int sect = 0; sect < 11; ++sect) {
                        wprintw(log_window, "------------ Look for sector sync %d ----------------\n", sect);
                        wrefresh(log_window);

                        if ( find_sync_marker(&track, &index) ) {
                                wprintw(log_window, "sync found @ index: %u\n", index);
                                wrefresh(log_window);
                                size_t byte_count = 0;
                                uint8_t *bitstream = samples_to_bitsream(&track, index, &byte_count);
                                if (!bitstream) {
                                        fprintf(stderr, "\t\tBitstream buffer allocation failed!\n");
                                        break;
                                }

                                int rc = parse_amiga_mfm_sector(bitstream, byte_count, &sector);
                                if (rc == 0) {
                                        const uint8_t track_no = (be32toh(sector.header_info) >> 16) & 0xff;
                                        const uint8_t sector_no = (be32toh(sector.header_info) >> 8) & 0xff;
                                        const uint8_t sector_to_gap = be32toh(sector.header_info) & 0xff;
                                        //wprintw(w, "-- [I] Track: %d - head: %d\n", track_info >> 1, track_info & 1);
                                        uint8_t sector_status = 0;
                                        if (!sector.data_checksum_ok) {
                                                sector_status |= 2;
                                        }
                                        if (!sector.header_checksum_ok) {
                                                sector_status |= 1;
                                        }
                                        int color = COLOR_PAIR(2); // Green
                                        switch(sector_status) {
                                        case 1:
                                                // Header bad
                                                color = COLOR_PAIR(3); // Cyan
                                                break;
                                        case 2:
                                                // Data bad
                                                color = COLOR_PAIR(4); // Yellow
                                                break;
                                        case 3:
                                                // Both bad
                                                color = COLOR_PAIR(5); // Red
                                                break;
                                        default:
                                                break;
                                        }

                                        mvwaddch(sector_window,
                                                 1 + sector_no + ((track_no & 1) ? 15 : 0),  /* ROW */
                                                 1 + (track_no >> 1), /* COL */
                                                ' ' | A_REVERSE | color);
                                        wrefresh(sector_window);
                                        free(sector.data);
                                }

                                free(bitstream);

                                index += 10;
                        } else {
                                //fprintf(stderr, "\t\tNo sync markers found in track %d - skipping\n", i);
                                break;
                        }
                        wprintw(log_window, "Sector %d done!\n", sect);
                        wrefresh(log_window);
                }

                wprintw(log_window, " ---------------- Track Done ---------------------\n");
                wrefresh(log_window);
                
                //r = revolutions - 1;
                //flux_data = flux_data_init(samples, index_offsets[r], index_offsets, revolutions);

                free(track.samples);
                // Track data is invalid now!
                memset(&track, 0x00, sizeof(track));

                free(index_offsets);
                /*
                printf("Read done! - \n");

                //samples2scp(&converted_data, &timing, samples, revolutions,
                //                                        index_offsets);

                printf("Converted! - \n");

                //add_scp_track(file, i, converted_data, timing);
                printf("Written\n");
                //free(timing);
                //free(converted_data);
                */

                if (i % 2) {
                        pru_step_head(pru, 1);
                        wprintw(log_window, "Step head!");
                        wrefresh(log_window);
                }
                //flux_data_free(flux_data);
        }
        pru_stop_motor(pru);

        mvwprintw(log_window, 10, 2, "DONE!");
        wrefresh(log_window);

        wgetch(log_window);

        delwin(log_window);
        delwin(sector_window);
        delwin(status_bar);
        endwin();

        //printf("ROWS: %d, COLS: %d\n", row, col);

        return 0;
}

struct amiga_sector_header_mfm {
        /**
         * uint16_t mfm_aaaa[2];
         */
        uint16_t mfm_4489[2];
        uint32_t info_odd;
        uint32_t info_even;
        uint32_t sector_label_odd[4];
        uint32_t sector_label_even[4];
        uint32_t header_checksum_odd;
        uint32_t header_checksum_even;
        uint32_t data_checksum_odd;
        uint32_t data_checksum_even;
        /**
         * uint8_t data_odd[512];
         * uint8_t data_even[512];
         */
};

/**
 * @brief       Try to parse a bitstream into an amiga standard sector.
 *
 * @detail      We expect that the bitstream is aligned so it starts on
 *              the standard amiga sector marker (0x4489 0x4489).
 *
 * @param       bitstream       The bitstream to parse
 * @param       byte_count      The length of the bitstream
 * @param       parsed_sector   This struct is populated with the parsed data.
 *
 * @return      Return 0 on success, any negative number is an error.
 */
static int parse_amiga_mfm_sector(const uint8_t *bitstream, size_t byte_count,
                                        struct amiga_sector *parsed_sector)
{
        memset(parsed_sector, 0x00, sizeof(*parsed_sector));

        if (byte_count < 1068) {
                fprintf(stderr, "Sector is not a standard amiga sector! - Expected 1068 bytes, found %u bytes.\n",
                                                                byte_count);
                return -1;
        }

        struct amiga_sector_header_mfm mfm_header;
        size_t stream_position = 0;

        memcpy(&mfm_header.mfm_4489, bitstream + stream_position, sizeof(mfm_header.mfm_4489));
        stream_position += sizeof mfm_header.mfm_4489;

        if ( be16toh(mfm_header.mfm_4489[0]) != 0x4489 || be16toh(mfm_header.mfm_4489[1]) != 0x4489 ) {
                fprintf(stderr, "Sector is not a standard amiga sector! - Missing sector header (0x4489)\n");
                return -2;
        }

        memcpy(&mfm_header.info_odd, bitstream + stream_position,
                                        sizeof(mfm_header.info_odd));
        stream_position += sizeof mfm_header.info_odd;
        memcpy(&mfm_header.info_even, bitstream + stream_position,
                                        sizeof(mfm_header.info_even));
        stream_position += sizeof mfm_header.info_even;

        memcpy(&mfm_header.sector_label_odd, bitstream + stream_position,
                                        sizeof(mfm_header.sector_label_odd));
        stream_position += sizeof mfm_header.sector_label_odd;
        memcpy(&mfm_header.sector_label_even, bitstream + stream_position,
                                        sizeof(mfm_header.sector_label_even));
        stream_position += sizeof mfm_header.sector_label_even;

        memcpy(&mfm_header.header_checksum_odd, bitstream + stream_position,
                                        sizeof(mfm_header.header_checksum_odd));
        stream_position += sizeof mfm_header.header_checksum_odd;
        memcpy(&mfm_header.header_checksum_even, bitstream + stream_position,
                                        sizeof(mfm_header.header_checksum_even));
        stream_position += sizeof mfm_header.header_checksum_even;

        memcpy(&mfm_header.data_checksum_odd, bitstream + stream_position,
                                        sizeof(mfm_header.data_checksum_odd));
        stream_position += sizeof mfm_header.data_checksum_odd;
        memcpy(&mfm_header.data_checksum_even, bitstream + stream_position,
                                        sizeof(mfm_header.data_checksum_even));
        stream_position += sizeof mfm_header.data_checksum_even;


        static const uint32_t mask = 0x55555555; // (0b01010101)

        parsed_sector->header_info = mfm_header.info_odd & mask;
        parsed_sector->header_info <<= 1;
        parsed_sector->header_info |= mfm_header.info_even & mask;

        uint32_t calculated_checksum = 0;
        calculated_checksum ^= mfm_header.info_odd;
        calculated_checksum ^= mfm_header.info_even;

        for ( unsigned int i = 0; i < 4; ++i) {
                parsed_sector->header_sector_label[i] = mfm_header.sector_label_odd[i] & mask;
                parsed_sector->header_sector_label[i] <<= 1;
                parsed_sector->header_sector_label[i] |= mfm_header.sector_label_even[i] & mask;

                calculated_checksum ^= mfm_header.sector_label_odd[i];
                calculated_checksum ^= mfm_header.sector_label_even[i];
        }

        calculated_checksum ^= mfm_header.header_checksum_odd;
        calculated_checksum ^= mfm_header.header_checksum_even;
        calculated_checksum &= mask;
        
        parsed_sector->header_checksum_ok = calculated_checksum == 0 ? true : false;

        uint32_t *sector_data = malloc(1024);
        if (!sector_data) {
                fprintf(stderr, "\t\t -- [E] Could not allocate data buffer for sector data!\n");
                return -4;
        }
        memcpy(sector_data, bitstream + stream_position, 1024);
        stream_position += 1024;

        calculated_checksum = 0;
        for (unsigned int i = 0; i < 512 / 4; ++i) {
                calculated_checksum ^= sector_data[i];
                calculated_checksum ^= sector_data[i + (512/4)];

                sector_data[i] &= mask;
                sector_data[i] <<= 1;
                sector_data[i] |= sector_data[i + (512 / 4)] & mask;
        }

        calculated_checksum ^= mfm_header.data_checksum_odd;
        calculated_checksum ^= mfm_header.data_checksum_even;
        calculated_checksum &= mask;

        parsed_sector->data_checksum_ok = calculated_checksum == 0 ? true : false;

        parsed_sector->data = sector_data;
        return 0;
}

/**
 * @brief       Convert an array of timing values to an array of raw data
 */
static uint8_t *samples_to_bitsream(struct track_samples *track, size_t index, size_t *_byte_count)
{
        *_byte_count = 0; // return paramater.

        // Maximum bits per sample is 4.
        const int bit_count = track->sample_count * 4;
        const int byte_count = 1 + (bit_count / 8);
        uint8_t *bitstream = malloc(byte_count);
        if (!bitstream) {
                fprintf(stderr, "%s: malloc failed!\n", __func__);
                return NULL;
        }
        memset(bitstream, 0x00, byte_count);

        //printf("Timing to bitstream, allocated %d bytes for %d bits\n", byte_count, bit_count);

        /**
         * We start at bit -2 as this is what the bitstream will look like from
         * the index we found for sync:
         *
         *     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
         *    01010001001000100101000100100010010
         *     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
         *
         * While the actual sync mark are the bits in the box.
         *
         *   xx|01000100100010010100010010001001|x
         *
         * Without the -2 here, the data will be offset by one bit.
         *
         * TODO:
         * Investigate if we can set `bit` to 0 and just change 
         * the index we return from find_sync_marker(...)
         *
         */
        unsigned int bit = -2; // See Note ^^^
        for (int i = index; i < track->sample_count; i++) {
                if (track->samples[i] > 700) {
                        bit += 4;
                } else if (track->samples[i] > 500) {
                        bit += 3;
                } else {
                        bit += 2;
                }
                bitstream[bit / 8] |= (1 << (7 - (bit % 8)));
        }

        //hexdump(bitstream, 512);
        //printf("Timing to bitstream, conversion done for %d bits\n", bit);
        *_byte_count = byte_count;
        return bitstream;
}

/**
 * @brief       Find sync markers (0x4489) in the timing sample data
 *              Setup the pointers to for the 11 sectors in the track struct.
 *              The pointers will point to the first sample of the sync marker
 *              in the sector.
 */
static bool find_sync_marker(struct track_samples *track, size_t *index)
{
        /**
         * A the most bits between flux transitions is 4
         * There are only three valid flux values:
         *
         *      01      This is 4 micro-seconds
         *      001     This is 6 micro-seconds
         *      0001    This is 8 micro-seconds.
         */
        enum sample_type { UNDEF, MS4, MS6, MS8 };

        /**
         * This is the series of flux transion timing data
         * we are looking for.
         * The following is the timing values to
         * encode 0x44894489 as MFM data timing.
         */

        static const enum sample_type sync[10] = {
                MS4, MS8, MS6, MS8, MS6, // 0x4489
                MS4, MS8, MS6, MS8, MS6  // 0x4489
        };

        /**
         * In this loop, we maintain the 10 latest
         * flux timing values in a ring buffer,
         * and compare it against the sync buffer
         * to see if they match.
         */
        enum sample_type ring_buffer[10] = {UNDEF};

        //int sector = 0;

        for (int i = *index; i < track->sample_count; i++) {
                if (track->samples[i] > 700) {
                        ring_buffer[i % 10] = MS8;
                } else if (track->samples[i] > 500) {
                        ring_buffer[i % 10] = MS6;
                } else {
                        ring_buffer[i % 10] = MS4;
                }

                int r_index = (i - 10);
                // Wait until the ringbuffer is full before we start comparing.
                if (r_index < 0) continue;

                bool found = true;
                for (int e = 0; e < 10; e++) {
                        if (sync[e] != ring_buffer[(r_index + e) % 10]) {
                                found = false;
                                break;
                        }
                }
                if (found) {
                        *index = i - 10;
                        return true;
                        /*
                        if (sector >= 11) {
                                // Current implementation only expects 11 sectors!
                                fprintf(stderr,
                                "%s: Error, found sector marker for sector: %d\n",
                                                        __func__, sector + 1);
                        } else {
                                // Setup pointers to start of sector in the track struct.
                                track->sectors[sector].samples =
                                                        &track->samples[i - 10];
                                printf(
                                "Sync for sector %d found at sample inndex: %d\n",
                                                                sector + 1, i);
                        }
                        sector++;
                        */
                }
        }
        return false;
}
