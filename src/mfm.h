#ifndef MFM_H
#define MFM_H
#include <stdint.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

// CLOCK = ODD_BITS 1,3,5,...
#define MFM_CLOCK_MASK 0xAAAAAAAA
// DATA = EVEN_BITS 0,2,4,...
#define MFM_DATA_MASK 0x55555555

struct raw_mfm_sector {
        uint32_t leading_zeroes;
        uint32_t sync_word;
        uint32_t odd_info;
        uint32_t even_info;
        uint32_t odd_label[4];
        uint32_t even_label[4];
        uint32_t odd_h_chksum;
        uint32_t even_h_chksum;
        uint32_t odd_d_chksum;
        uint32_t even_d_chksum;
        uint32_t odd_data[512/4];
        uint32_t even_data[512/4];
}__attribute__((packed));

struct mfm_sector_header {
        uint32_t info;
        uint32_t label[4];
        uint32_t header_chksum;
        uint32_t data_chksum;
        uint32_t data[512/4];
        int      head_chksum_ok;
        int      data_chksum_ok;
};
#define MFM_INFO_GET_FMT(info) \
        (info & 0xff)
#define MFM_INFO_GET_CYL(info) \
        (((info & 0xff00) >> 8) >> 1)
#define MFM_INFO_GET_HEAD(info) \
        (((info & 0xff00) >> 8) & 1)
#define MFM_INFO_GET_SEC(info) \
        ((info & 0xff0000) >> 16)
#define MFM_INFO_GET_GAP(info) \
        ((info & 0xff000000) >> 24)

#define MFM_INFO_PRINT(info) \
        printf("%02x: Sector: %2d - gap: %2d - cylinder: %2d, head: %d\n", \
                MFM_INFO_GET_FMT(info), MFM_INFO_GET_SEC(info),            \
                MFM_INFO_GET_GAP(info), MFM_INFO_GET_CYL(info),            \
                                        MFM_INFO_GET_HEAD(info))

#endif

