#include "caps_parser.h"
#include "../write_flux_opts.h"
#include "../mfm_utils/mfm_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define die(...) do { \
                fprintf(stderr, __VA_ARGS__); \
                exit(EXIT_FAILURE); \
        } while(0)

// 4 us 01
// 6 us 001
// 8 us 0001

struct sector_samples {
        uint32_t *samples; // Pointer to first sample of sync marker (0x4489)
        int samples_count;
        int sector_number;
};
struct track_samples {
        uint32_t *samples; // Raw sample data, heap allocated
        int sample_count;
        struct sector_samples sectors[11];
};

static void __attribute__((__unused__)) hexdump(const void *data, size_t len);
static void __attribute__((__unused__)) write_data_to_disk(const struct write_flux_opts *opts, struct caps_parser *parser);
static int bitstream_to_timing(uint32_t *samples, const uint8_t *bitstream, size_t track_size);
static bool find_sync_marker(struct track_samples *track, size_t *index);
static size_t timing_sample_to_bitstream(const uint32_t * restrict samples, size_t samples_count,
                                        uint8_t * restrict bitstream, size_t bitstream_size);

int main(int argc, char *argv[])
{
        struct write_flux_opts opts;
        bool rc = write_flux_opts_parse(&opts, argc, argv);
        if (!rc) {
                write_flux_opts_print_usage(argv);
                exit(EXIT_FAILURE);
        }

        FILE *ipf_img = fopen(opts.filename, "rb");
        if (!ipf_img) {
                die("Could not open disk image \"%s\": %s\n",
                                opts.filename, strerror(errno));
        }

        struct caps_parser *parser = caps_parser_init(ipf_img);
        if (!parser) {
                die("Could not initialize caps_parser\n");
        }

        if (opts.image_info_only) {
                caps_parser_show_file_info(parser);
                caps_parser_show_den_types(parser);
                exit(EXIT_SUCCESS);
        }

        uint32_t *samples = malloc(sizeof(*samples) * (1u << 16));
        if (!samples) {
                die("Could not initialize sample buffer\n");
        }

        uint8_t *disk_track_mfm_bitstream = malloc(1088 * 11); // Amiga mfm sector byte size times 11 sectors.
        if (!disk_track_mfm_bitstream) {
                die("Could not initialize mfm buffer\n");
        }

        // write_data_to_disk(&opts, parser);

        const struct CapsImage * track_data = NULL;

        unsigned last_track = (opts.track == -1 ? 79 : opts.track) * 2;
        unsigned track = opts.track == -1 ? 0 : opts.track * 2;

        if (opts.head == -1) {
                last_track += 2;
        } else if (opts.head == 1) {
                track++;
                last_track++;
        }

        do {

                memset(disk_track_mfm_bitstream, 0xaa, 1088 * 11);

                uint8_t head = track & 0x01;
                uint8_t cylinder = track / 2;

                bool ret = caps_parser_get_caps_image_for_track_and_head(
                                        parser, &track_data, cylinder, head);
                if (!ret) {
                        die("Could not find track %u - head %u in ipf file: %s\n",
                                                cylinder, head, opts.filename);
                }

                uint8_t *bitstream = caps_parser_get_bitstream_for_track(parser, track_data);
                if (!bitstream) {
                        die("Could not read bitstream for track %u - head %u in ipf file: %s\n",
                                                cylinder, head, opts.filename);
                }

                printf("------------- Parsee bitstream -------------\n");
                for (int i = 0; i < 11; ++i) {
                        printf("Sector: %d\n", i);
                        // hexdump(&bitstream[1088 * i], 16); // Dump start of sector
                        // hexdump(&bitstream[(1088 * i) + 1084], 16); // dump end of sector

                        // printf("Sector: %d\n", i);
                        // hexdump(&bitstream[(1088 * i) + 4], 16);
                        struct amiga_sector sector;
                        int rc = parse_amiga_mfm_sector(&bitstream[(1088 * i) + 4], 1084, &sector, NULL /* Don't keep sector data */);
                        if (rc == 0) {
                                /*
                                const uint8_t track_no = (be32toh(sector.header_info) >> 16) & 0xff;
                                const uint8_t sector_no = (be32toh(sector.header_info) >> 8) & 0xff;
                                printf("-- [I] Track: %d - head: %d - sector: %u\n", track_no >> 1, track_no & 1, sector_no);
                                */
                                if (!sector.header_checksum_ok) {
                                        printf("BAD Header CRC track: %u - head %u - sector: %i\n", cylinder, head, i);
                                }
                                if (!sector.data_checksum_ok) {
                                        printf("BAD Data CRC track: %u - head %u - sector: %i\n", cylinder, head, i);
                                }
                                if (i == 10) {
                                        // hexdump(bitstream + (1088 * i) + 8, 1080);
                                }
                                /*
                                printf("CRC ok: %s %s\n",
                                        sector.header_checksum_ok ? "OK " : "BAD",
                                        sector.data_checksum_ok ? "OK " : "BAD");
                                */
                        } else {
                                printf("Failed to parse mfm sector!\n");
                        }
                }

                printf("------------- bitstream to timing -----------------\n");
                // ----------------------------------------------------------------------------
                memset(samples, 0x00, sizeof(*samples) * (1u << 16));
                const uint32_t trkbits = be32toh(track_data->trkbits);
                if (trkbits & 0x7) {
                        fprintf(stderr, "\033[0;31mtrkbits is not divisible!\033[0m\n");
                }
                size_t track_size = trkbits >> 3; // Div. 8 to get bytes.
                // track_size += trkbits & 0x7 ? 1 : 0;
                int sample_count = bitstream_to_timing(samples, bitstream, track_size);
                struct track_samples tt = {
                 .sample_count = sample_count,
                 .samples = samples
                };

#if 0
                char filename[255] = {0};
                snprintf(filename, sizeof(filename), "t%u-good.raw", track);
                FILE *fp = fopen(filename, "wb");
                if (fp) {
                        fwrite(bitstream, sizeof(*bitstream), track_size, fp);
                        fclose(fp);
                } else {
                        fprintf(stderr, "Failed to open file: %s\n", filename);
                }
#endif


                size_t index = 0;
                if (find_sync_marker(&tt, &index)) {
                        memset(disk_track_mfm_bitstream, 0x00, 1088 * 11);
                        size_t consumed = timing_sample_to_bitstream(
                                        tt.samples + index,
                                        tt.sample_count - index,
                                        disk_track_mfm_bitstream, 1088 * 11);
                        (void) consumed;
                        struct amiga_sector sector;
                        int rc = parse_amiga_mfm_sector(disk_track_mfm_bitstream, 1084, &sector, NULL /* Don't keep sector data */);
                        if (rc == 0) {
                                const uint8_t track_no = (be32toh(sector.header_info) >> 16) & 0xff;
                                const uint8_t sector_no = (be32toh(sector.header_info) >> 8) & 0xff;
                                printf("-- [I] Track: %d - head: %d - sector: %u\n", track_no >> 1, track_no & 1, sector_no);
                                if (!sector.header_checksum_ok) {
                                        printf("BAD Header CRC track: %u - head %u - sector: %i\n", cylinder, head, 0);
                                }
                                if (!sector.data_checksum_ok) {
                                        printf("BAD Data CRC track: %u - head %u - sector: %i\n", cylinder, head, 0);
                                }
                                /*
                                printf("CRC ok: %s %s\n",
                                        sector.header_checksum_ok ? "OK " : "BAD",
                                        sector.data_checksum_ok ? "OK " : "BAD");
                                */
                        } else {
                                printf("Failed to parse mfm sector!\n");
                        }
                }

                free(bitstream);

                track += opts.head == -1 ? 1 : 2;

        } while(track < last_track);

        free(disk_track_mfm_bitstream);

#if 0
        for (int i = 0; i < 11; ++i) {
                printf("Sector: %d\n", i);
                // hexdump(&bitstream[1088 * i], 16); // Dump start of sector
                // hexdump(&bitstream[(1088 * i) + 1084], 16); // dump end of sector

                // printf("Sector: %d\n", i);
                // hexdump(&bitstream[(1088 * i) + 4], 16);
                struct amiga_sector sector;
                int rc = parse_amiga_mfm_sector(&bitstream[(1088 * i) + 4], 1084, &sector, NULL /* Don't keep sector data */);
                if (rc == 0) {
                        /*
                        const uint8_t track_no = (be32toh(sector.header_info) >> 16) & 0xff;
                        const uint8_t sector_no = (be32toh(sector.header_info) >> 8) & 0xff;
                        printf("-- [I] Track: %d - head: %d - sector: %u\n", track_no >> 1, track_no & 1, sector_no);
                        */
                        if (!sector.header_checksum_ok) {
                                printf("BAD Header CRC track: %u - head %u - sector: %i\n", cylinder, head, i);
                        }
                        if (!sector.data_checksum_ok) {
                                printf("BAD Data CRC track: %u - head %u - sector: %i\n", cylinder, head, i);
                        }
                        if (i == 10) {
                                // hexdump(bitstream + (1088 * i) + 8, 1080);
                        }
                        /*
                        printf("CRC ok: %s %s\n",
                                sector.header_checksum_ok ? "OK " : "BAD",
                                sector.data_checksum_ok ? "OK " : "BAD");
                        */
                } else {
                        printf("Failed to parse mfm sector!\n");
                }
        }
#endif

        free(samples);

        return 0;
}

/**
 * Convert mfm-bitstream to uint32_t timing samples
 * each sample is a count of 10 nano-seconds.
 */
static int bitstream_to_timing(uint32_t *samples, const uint8_t *bitstream, size_t track_size)
{
        int sample_index = 0;

        unsigned bit_count = 0;
        uint8_t counter = 0;
        for (size_t i = 0; i < track_size; ++i) {
                const uint8_t b = bitstream[i];
                uint8_t bit = 0x80;
                while(bit) {
                        counter++;
                        if (b & bit) {
                                // printf("Count: %u\n", counter);
                                if (counter == 4) {
                                        samples[sample_index++] = 800;
                                } else if (counter == 3) {
                                        samples[sample_index++] = 600;
                                } else if (counter == 2) {
                                        samples[sample_index++] = 400;
                                }
                                counter = 0;
                                bit_count++;
                        }
                        bit >>= 1;
                        if (sample_index >= (1u << 16)) {
                                printf("Stream index: %d / %d\n", i, track_size);
                                die("Sample buffer overflow!");
                        }
                }
        }
        /*
        printf("Track size: %d\n", track_size);
        printf("Bit count: %u (samples: %d)\n", bit_count, sample_index);
        */
        return sample_index;
}

static void write_data_to_disk(const struct write_flux_opts *opts, struct caps_parser *parser)
{
        const struct CapsImage *track_info = NULL;

        unsigned last_track = (opts->track == -1 ? 79 : opts->track) * 2;
        unsigned track = opts->track == -1 ? 0 : opts->track * 2;
        if (opts->head == -1) {
                last_track += 2;
        } else if (opts->head == 1) {
                track++;
                last_track++;
        }

        do {
                uint8_t head = track & 0x01;
                bool ret = caps_parser_get_caps_image_for_track_and_head(
                                        parser, &track_info, track / 2, head);
                if (!ret) {
                        fprintf(stderr,
                                "Could not find track %u - head %u in ipf file: %s\n",
                                                track, head, opts->filename);
                        return;
                }

                printf("Read track: %u, head: %u\n", track / 2, head);

                track += opts->head == -1 ? 1 : 2;

        } while(track < last_track);
}

static size_t timing_sample_to_bitstream(const uint32_t * restrict samples, size_t samples_count,
                                         uint8_t * restrict bitstream, size_t bitstream_size)
{
        // clear bitstream..
        memset(bitstream, 0x00, bitstream_size);
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
        size_t i = 0;
        for (; i < samples_count; i++) {
                if (samples[i] > 700) {
                        bit += 4;
                } else if (samples[i] > 500) {
                        bit += 3;
                } else {
                        bit += 2;
                }
                const int byte_no = bit >> 3;  // Div. 8
                const int bit_no = bit & 0x07; // Mod. 8
                if (byte_no >= bitstream_size) {
                        // Bitstream full.
                        printf("Bitstream buffer full!\n");
                        return i + 1;
                }
                bitstream[byte_no] |= (1 << (7 - (bit_no)));
        }

        return i;
}

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
        // TODO: I'd assume the first sample should be MS6 instead of MS4
        //       as 0xaa is preceeding the sync marker so the preceding bits were 0b10
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

        /*
         * Multiply count with 10 to get nSec
         * Divide by 100 to get μSec
         */

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
                if (r_index < 0) {
                        continue;
                }

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

static void hexdump(const void *rdata, size_t len)
{
        const uint8_t *data = rdata;
        uint8_t c[17];
        memset(c, 0x00, 17);
        unsigned int i = 0;
        for (; i < len; ++i) {
                if (i && (i % 16 == 0)) {
                        printf("   %s\n", c);
                }
                printf("%02x ", data[i]);
                c[i % 16] = (data[i] > 32 && data[i] < 127) ? data[i] : '.';
        }

        printf("   %s\n", c);
}

