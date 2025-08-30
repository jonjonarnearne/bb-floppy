#include "stdio.h"
#include "stdlib.h"
#include "stdint.h"

/**
 * @brief	Helper to convert ipf samples to mfm data
 */
void hexdump(const void *b, size_t len);
static inline uint16_t ipf_to_mfm(uint8_t ipf);
static uint16_t *parse_ipf_samples(const uint8_t *samples, size_t num_samples,
                                                        uint16_t prev_sample);

int main(int argc, char *argv[])
{
        (void) argc;
        (void) argv;

        uint8_t samples[] = {
                0x00, 0x00, 0x00, 0x00
        };

        /**
         * If last MFM byte had bit 0 set, we can not set bit 8 of next byte
         * as that could lead to this bitstream:
         *
         *       PREV MFM  |  This MFM
         *            xxx1 | 1xxxxx
         *
         * which is illegal MFM - to prevent this, we pass in last MFM byte
         * to parse ipf samples to make sure we fix issues like this.
         */
        uint16_t *result = parse_ipf_samples(samples, sizeof(samples), be16toh(0x0001));
        hexdump(result, sizeof(samples) * 2);
        free(result);

        uint8_t val = 0;
        for(int i = 0; i < 256; ++i) {
                printf("%02x ", val);
                uint16_t ipf_val = ipf_to_mfm(val);
                hexdump(&ipf_val, 2);
                val++;
        }

        return EXIT_SUCCESS;
}

/**
 * @brief	Helper to convert ipf samples to mfm data
 */
static inline uint16_t ipf_to_mfm(uint8_t ipf)
{
        uint16_t mfm = 0;
        size_t bit = 0x80;

        while(bit) {
                mfm <<= 2;
                if (ipf & bit) {
                        // Set data bit
                        mfm |= 0x01;
                } else if ((mfm & 0x04) == 0) {
                        // Add clock bit if needed
                        mfm |= 0x02;
                }
                bit >>= 1;
        }

        return htobe16(mfm);
}

static uint16_t *parse_ipf_samples(const uint8_t *samples, size_t num_samples,
                                                        uint16_t prev_sample)
{
        uint16_t *mfm_samples = malloc(num_samples * 2);
        if (!mfm_samples) {
                fprintf(stderr, "mfm sample alloc failed!\n");
                return NULL;
        }

        for (unsigned int i = 0; i < num_samples; ++i) {
                mfm_samples[i] = ipf_to_mfm(samples[i]);
                if (prev_sample & htobe16(0x0001)) {
                        // Clear bit 15
                        mfm_samples[i] &= ~htobe16(1 << 15);
                }
                prev_sample = mfm_samples[i];
        }

        // hexdump(mfm_samples, num_samples * 2);

        return mfm_samples;
}

void hexdump(const void *b, size_t len)
{
    int i;
    const uint8_t *buf = b;
    char str[17];
    char *c = str;

    for (i=0; i < len; i++) {
        if (i && !(i % 16)) {
            *c = '\0';
            printf("%s\n", str);
            c = str;
        }
        if (!(i % 16)) {
                printf("0x%04X: ", i);
        }
        *c++ = (*buf < 128 && *buf > 32) ? *buf : '.';
        printf("%02x ", *buf++);
    }

    *c = '\0';
    printf("%s\n", str);
}
