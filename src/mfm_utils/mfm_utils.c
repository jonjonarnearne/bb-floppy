#include <endian.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mfm_utils.h"

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
int parse_amiga_mfm_sector(const uint8_t *bitstream, size_t byte_count,
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

        //printf("Header Checksum: %08x\n", calculated_checksum);

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

        //printf("Data Checksum: %08x\n", calculated_checksum);

        calculated_checksum ^= mfm_header.data_checksum_odd;
        calculated_checksum ^= mfm_header.data_checksum_even;
        calculated_checksum &= mask;



        parsed_sector->data_checksum_ok = calculated_checksum == 0 ? true : false;

        parsed_sector->data = sector_data;
        return 0;
}

