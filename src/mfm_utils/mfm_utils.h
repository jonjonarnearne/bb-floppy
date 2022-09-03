#ifndef MFM_UTILS_H
#define MFM_UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

struct amiga_sector {
        uint32_t header_info;
        uint32_t header_sector_label[4];
        uint32_t *data;
        bool header_checksum_ok;
        bool data_checksum_ok;
};

int parse_amiga_mfm_sector(const uint8_t *bitstream, size_t byte_count,
                                        struct amiga_sector *parsed_sector);

#endif /* MFM_UTILS_H */
