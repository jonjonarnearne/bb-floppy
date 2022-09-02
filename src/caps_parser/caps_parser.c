#include <assert.h>
#include <endian.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "caps_parser.h"

static void __attribute__((__unused__)) parse_ipf_samples(uint8_t *samples,
                                                        size_t num_samples);

static void __attribute__((__unused__)) print_caps_image(
                                                struct CapsImage *caps_image);
static void __attribute__((__unused__)) print_caps_data(
                                                struct CapsData *caps_data);
static void __attribute__((__unused__)) print_caps_block(
                                                struct CapsBlock *caps_block);

static const char __attribute__((__unused__)) * platform_to_string(
                                                        uint32_t platform_id);
static const char __attribute__((__unused__)) * dentype_to_string(
                                                        uint32_t dentype);
static const char __attribute__((__unused__)) * sampletype_to_string(
                                                        uint32_t sampletype);

static void __attribute__((__unused__)) hexdump(void *data, size_t len);

struct caps_parser *caps_parser_init(FILE *fp)
{
        static_assert(sizeof(struct caps_header) == 12, "Size assertion failed!");

        struct caps_parser *p = malloc(sizeof(*p));
        if (!p) {
                return NULL;
        }
        p->fp = fp;
        p->caps_list_head.next = NULL;

        int rc = fread(&p->caps_list_head.header, sizeof p->caps_list_head.header, 1, p->fp);
        if (rc != 1) {
                return NULL;
        }
        p->caps_list_head.header.name = htobe32(p->caps_list_head.header.name);
        p->caps_list_head.header.len = be32toh(p->caps_list_head.header.len);

        p->caps_list_head.fpos = ftell(p->fp);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmultichar"
        if (p->caps_list_head.header.name != 'CAPS') {
#pragma GCC diagnostic pop
                // This is not a CAPS file!
                return NULL;
        }

        uint32_t extra_len = 0;
        struct caps_node *node = &p->caps_list_head;
        while(true) {
                struct caps_node *n = malloc(sizeof(*n));
                if (!n) {
                        // Memory error!
                        caps_parser_cleanup(p);
                        return NULL;
                }

                n->next = NULL;

                rc = fread(&n->header, sizeof n->header, 1, p->fp);
                if (rc != 1) {
                        free(n);
                        if (!feof(p->fp)) {
                                fprintf(stderr, "Read error!\n");
                                caps_parser_cleanup(p);
                                return NULL;
                        }
                        // End of file!
                        break;
                }

                n->header.name = be32toh(n->header.name);
                n->header.len = be32toh(n->header.len);
                n->fpos = ftell(p->fp);

                const uint32_t printable_name = htobe32(n->header.name);
                printf("Name: %.4s\n", (char *)&printable_name);
                unsigned int expected_len = sizeof n->header;
                switch (n->header.name) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmultichar"
                case 'INFO':
#pragma GCC diagnostic pop
                        expected_len += sizeof n->chunk.info;
                        rc = fread(&n->chunk.info, sizeof n->chunk.info, 1, p->fp);
                        break;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmultichar"
                case 'IMGE':
#pragma GCC diagnostic pop
                        expected_len += sizeof n->chunk.imge;
                        rc = fread(&n->chunk.imge, sizeof n->chunk.imge, 1, p->fp);
                        break;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmultichar"
                case 'DATA':
#pragma GCC diagnostic pop
                        expected_len += sizeof n->chunk.data;
                        rc = fread(&n->chunk.data, sizeof n->chunk.data, 1, p->fp);
                        break;
                default:
                        fprintf(stderr, "Got unexpected caps chunk: %.4s\n",
                                        (char *)&printable_name);
                        rc = 0;
                }
                if (rc != 1) {
                        free(n);
                        fprintf(stderr, "Failed to read caps chunk!\n");
                        caps_parser_cleanup(p);
                        return NULL;
                }

                if (n->header.len != expected_len) {
                        free(n);
                        caps_parser_cleanup(p);
                        fprintf(stderr,
                                "Integrity error, expected_len: %u - actual_len: %u\n",
                                                        expected_len, n->header.len);
                        return NULL;
                }

                extra_len = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmultichar"
                if (n->header.name == 'DATA') {
#pragma GCC diagnostic pop
                        /**
                         * The len reported in header is not correct when we
                         * encounter the DATA chunks.
                         * We have to parse the chunk to get the correct len
                         */
                        extra_len = be32toh(n->chunk.data.size);
                }

                // Move pointer to end of chunk
                fseek(p->fp, (n->fpos + n->header.len - sizeof(struct caps_header) + extra_len), SEEK_SET);

                node->next = n;
                node = node->next;
        }

        printf("Init done!\n");
        return p;
}

void caps_parser_cleanup(struct caps_parser *p)
{
        struct caps_node *node = &p->caps_list_head;
        struct caps_node *next = node->next;
        while(next) {
                struct caps_node *this = next;
                next = next->next;
                free(this);
        }
        free(p);
}

bool caps_parser_get_caps_image_for_did(struct caps_parser *p,
                                        struct CapsImage **caps_image,
                                        uint32_t did)
{
        struct caps_node *node = p->caps_list_head.next;
        while(node) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmultichar"
                if (node->header.name == 'IMGE') {
#pragma GCC diagnostic pop
                        if (be32toh(node->chunk.imge.did) != did) {
                                // Keep looking
                                goto next_node;
                        }

                        *caps_image = &node->chunk.imge;
                        return true;
                }
next_node:
                node = node->next;
        }

        return false;
}

void caps_parser_show_file_info(struct caps_parser *p)
{
        struct caps_node *node = p->caps_list_head.next;
        bool found = false;
        while(node) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmultichar"
                if (node->header.name == 'INFO') {
#pragma GCC diagnostic pop
                        found = true;
                        break;
                }
                node = node->next;
        }
        if (!found) {
                printf("No INFO chunk found in IPS data!\n");
                return;
        }

        printf("*------------------------------------------------------------------------\n");
        printf("|                                CapsInfo:\n");
        printf("*------------------------------------------------------------------------\n");
        printf("| Image type: %s\n", be32toh(node->chunk.info.type) == 1 ? "FDD" :
                                   be32toh(node->chunk.info.type) == 0 ? "N/A" :
                                                 "UNKNOWN VERSION");
        printf("| Encoder: %s\n", be32toh(node->chunk.info.encoder) == 2 ? "RAW" :
                                be32toh(node->chunk.info.encoder) == 1 ? "MFM" :
                                be32toh(node->chunk.info.encoder) == 0 ? "N/A" :
                                                 "UNKNOWN VERSION");
        printf("| Encoder revision: %d\n", be32toh(node->chunk.info.encrev));
        printf("| Release: %d\n", be32toh(node->chunk.info.release));
        printf("| Revision: %d\n", be32toh(node->chunk.info.revision));
        printf("| Original source ref.: 0x%08x\n", be32toh(node->chunk.info.origin));
        printf("| Min. cylinder: %d\n", be32toh(node->chunk.info.mincylinder));
        printf("| Max. cylinder: %d\n", be32toh(node->chunk.info.maxcylinder));
        printf("| Min. head: %d\n", be32toh(node->chunk.info.minhead));
        printf("| Max. head: %d\n", be32toh(node->chunk.info.maxhead));
        printf("| Date: 0x%08x (%u)\n", be32toh(node->chunk.info.date),
                                      be32toh(node->chunk.info.date));
        printf("| Time: 0x%08x (%u)\n", be32toh(node->chunk.info.time),
                                      be32toh(node->chunk.info.time));
        printf("| Platforms: [");
        for (unsigned int i = 0; i < 4; ++i) {
                const uint32_t id = be32toh(node->chunk.info.platform[i]);
                if (id == 0 || id > 9) {
                        // Platform is N/A. or above max.
                        continue;
                }
                if (i > 0) {
                        printf(", ");
                }
                printf("%s", platform_to_string(be32toh(node->chunk.info.platform[i])));
        }
        printf("]\n");
        printf("| Disk #: %d\n", be32toh(node->chunk.info.disknum));
        printf("| User Id: 0x%08x\n", be32toh(node->chunk.info.userid));
        printf("*------------------------------------------------------------------------\n");
}

/**
 * @brief       Print info about a track
 *
 * @detail      This is stored under the IMGE header.
 */
void caps_parser_show_track_info(struct caps_parser *p, unsigned int cylinder,
                                                        unsigned char head)
{
        unsigned int count = 0;
        struct caps_node *node = p->caps_list_head.next;
        while(node) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmultichar"
                if (node->header.name == 'IMGE') {
#pragma GCC diagnostic pop
                        if (be32toh(node->chunk.imge.cylinder) != cylinder ||
                            be32toh(node->chunk.imge.head) != head) {
                                // Keep looking
                                count++;
                                goto next_node;
                        }

                        // Done!
                        print_caps_image(&node->chunk.imge);
                        return;
                }
next_node:
                node = node->next;
        }

        // We only drop out of this loop if we dindn't find what we was looking for!
        fprintf(stderr, "Could not find IMGE data for cyl: %u head: %u\n",
                                                        cylinder, head);
}

void caps_parser_show_data(struct caps_parser *p, uint32_t did)
{
        // We need a caps image to get all data from the CapsData block.
        struct CapsImage *caps_image;
        bool rc = caps_parser_get_caps_image_for_did(p, &caps_image, did);
        if (!rc) {
                fprintf(stderr, "Could not get CapsImage for did: %u\n", did);
                return;
        }

        bool found = false;
        struct caps_node *node = p->caps_list_head.next;
        while(node) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmultichar"
                if (node->header.name == 'DATA') {
#pragma GCC diagnostic pop
                        if (be32toh(node->chunk.data.did) == did) {
                                found = true;
                                break;
                        }
                }
                node = node->next;
        }

        if (!found) {
                fprintf(stderr, "Could not find DATA data for did: %u\n", did);
                return;
        }

        print_caps_data(&node->chunk.data);

        // First set this to start pos of block data.
        long expected_end_fpos = node->fpos + sizeof node->chunk.data;
        fseek(p->fp, expected_end_fpos,  SEEK_SET);

        uint32_t offsets[11] = {0};
        struct CapsBlock caps_block;
        for (unsigned int i = 0; i < be32toh(caps_image->blkcnt); ++i) {
                int r = fread(&caps_block, sizeof caps_block, 1, p->fp);
                if (r != 1) {
                        fprintf(stderr, "Failed to read CapsBlock out of file\n");
                        return;
                }
                printf("Block %u\n", i);
                if (i == 0) {
                        expected_end_fpos += be32toh(caps_block.dataoffset);
                }
                print_caps_block(&caps_block);

                if (i < 11) {
                        offsets[i] = be32toh(caps_block.dataoffset);
                } else {
                        fprintf(stderr, "Warning .. Sector no: %u - not storing offset!\n", i);
                }
        }

        const long fpos = node->fpos + sizeof node->chunk.data;
        const long cur = ftell(p->fp);
        printf("Node pos: %ld - Cur pos: %ld - Diff: %ld\n",
                                        fpos, cur, cur - fpos);
        if (expected_end_fpos != cur) {
                fprintf(stderr,
                        "Integrety error Expected fpos: %ld - Current fpos: %ld\n",
                                        expected_end_fpos, cur);
        }

        /*** READ SAMPLES ***/
        printf("Sample size: %u\n",
                be32toh(node->chunk.data.size) - (sizeof caps_block  * 11));

        uint8_t sampledata_len = be32toh(node->chunk.data.size) - (sizeof caps_block * 11);
        uint8_t *sampledata = malloc(sampledata_len);
        if (!sampledata) {
                fprintf(stderr, "Memory allocation for sampledata failed!\n");
                return;
        }

        int ret = fread(sampledata, 1, sampledata_len, p->fp);
        if (ret != sampledata_len) {
                fprintf(stderr, "Couldn't read sample data from disk!\n");
                free(sampledata);
                return;
        }

        /*** PARSE SAMPLES ***/
        uint8_t *ptr = sampledata;
        const uint8_t *endptr = ptr + (offsets[1] - offsets[0]);
        while(ptr < endptr) {
                uint8_t sample_head = *ptr++;
                uint8_t sample_type = sample_head & 0x1f;
                uint8_t sizeof_sample_len = sample_head >> 5;
                if (sizeof_sample_len > 4) {
                        fprintf(stderr,
                                "Sample parsing failed on bad `sizeof_sample_len`: %u\n",
                                                                        sizeof_sample_len);
                        break;
                }
                
                //TODO: This needs fixing..
                //
                // Will read 0x02 0x1c as 7170 instead of 540.
                //
                uint32_t num_samples = 0;
                memcpy(&num_samples, ptr, sizeof_sample_len);

                ptr += sizeof_sample_len;
                printf("Num samples: %u of type: %s (%u)\n", num_samples,
                                sampletype_to_string(sample_type), sample_type);
                hexdump(&num_samples, 4);

                if (sample_type == 2 || sample_type == 3) {
                        // data || gap -- IPF_encoded
                        //parse_ipf_samples(ptr, num_samples);
                } else if (sample_type == 1) {
                        // mark/sync
                        hexdump(ptr, num_samples);
                } else {
                        fprintf(stderr, "Unexpected sample type: %u\n", sample_type);
                }
                ptr += num_samples;
        }

        free(sampledata);
}

static inline uint16_t ipf_to_mfm(uint8_t ipf)
{
        uint16_t mfm = 0;
        size_t bit = 0x80;

        while(bit) {
                mfm <<= 2;
                if (ipf & bit) {
                        // Set data bit
                        mfm |= 0x01;
                } else if ((ipf & 0x04) == 0) {
                        // Add clock bit if needed
                        mfm |= 0x02;
                }
                bit >>= 1;
        }

        return mfm;
}

static void parse_ipf_samples(uint8_t *samples, size_t num_samples)
{
        uint16_t *mfm_samples = malloc(num_samples * 2);
        if (!mfm_samples) {
                fprintf(stderr, "mfm sample alloc failed!\n");
                return;
        }

        for (unsigned int i = 0; i < num_samples; ++i) {
                mfm_samples[i] = ipf_to_mfm(samples[i]);
        }

        hexdump(mfm_samples, num_samples * 2);
        free(mfm_samples);
}

/**
 * @brief       Private function to give details of CapsImage
 */
static void print_caps_image(struct CapsImage *caps_image)
{
        printf("*------------------------------------------------------------------------\n");
        printf("|                                CapsImage:\n");
        printf("*------------------------------------------------------------------------\n");
        printf("| Cylinder: %u\n", be32toh(caps_image->cylinder));
        printf("| Head: %u\n", be32toh(caps_image->head));
        printf("| Dentype: %s\n", dentype_to_string(be32toh(caps_image->dentype)));
        printf("| Sigtype: %s\n", be32toh(caps_image->sigtype) == 1 ? "2 us cell" : "N/A");
        printf("| Track Size, rounded: %u\n", be32toh(caps_image->trksize));
        printf("| Start position, rounded: %u\n", be32toh(caps_image->startpos));
        printf("| Start bit (original data): %u\n", be32toh(caps_image->startbit));
        printf("| Decoded data size in bits: %u\n", be32toh(caps_image->databits));
        printf("| Decoded gap size in bits: %u\n", be32toh(caps_image->gapbits));
        printf("| Decoded track size in bits: %u\n", be32toh(caps_image->trkbits));
        printf("| Block count: %u\n", be32toh(caps_image->blkcnt));
        printf("| Encoder process: %s\n", be32toh(caps_image->process) == 2 ? "RAW" :
                                          be32toh(caps_image->process) == 1 ? "MFM" :
                                                                              "N/A");
        printf("| Image flags: 0x%08x\n", be32toh(caps_image->flag));
        printf("| Data chunk identifier (did): %u\n", be32toh(caps_image->did));
        printf("*------------------------------------------------------------------------\n");
}

static void print_caps_data(struct CapsData *caps_data)
{
        printf("*------------------------------------------------------------------------\n");
        printf("|                                CapsData:\n");
        printf("*------------------------------------------------------------------------\n");
        printf("| Data area size: %u\n", be32toh(caps_data->size));
        printf("| Data area bit size: %u\n", be32toh(caps_data->bsize));
        printf("| Data area crc: 0x%08x\n", be32toh(caps_data->dcrc));
        printf("| did: %u\n", be32toh(caps_data->did));
        printf("*------------------------------------------------------------------------\n");
}

static void print_caps_block(struct CapsBlock *caps_block)
{
        printf("*------------------------------------------------------------------------\n");
        printf("|                                CapsBlock:\n");
        printf("*------------------------------------------------------------------------\n");
        printf("| Decoded block size (bits): %u\n", be32toh(caps_block->blockbits));
        printf("| Decoded gap size (bits): %u\n", be32toh(caps_block->gapbits));
        printf("| (BlockSize decoded) Gapoffset in data area: %u\n", be32toh(caps_block->bt.sps.gapoffset));
        printf("| (Gapsize decoded) Bitcell type: (%u )%s\n",
                        be32toh(caps_block->bt.sps.celltype),
                        be32toh(caps_block->bt.sps.celltype) == 1 ? "2 us cell"
                                                                  : "N/A");
        printf("| Encoder type: (%u) %s\n",
                        be32toh(caps_block->enctype),
                        be32toh(caps_block->enctype) == 2 ? "RAW" :
                        be32toh(caps_block->enctype) == 1 ? "MFM" :
                                                            "N/A");
        printf("| Flags: 0x%08x\n", be32toh(caps_block->flag));
        printf("| Default gap value: %u\n", be32toh(caps_block->gapvalue));
        printf("| Data offset in data area: %u\n", be32toh(caps_block->dataoffset));
        printf("*------------------------------------------------------------------------\n");
}

/**
 * @brief       Print platform name based on CapsInfo.type
 */
static const char *platform_to_string(uint32_t platform_id)
{
        switch(platform_id) {
        case 1:
                return "Amiga";
        case 2:
                return "AtariST";
        case 3:
                return "PC";
        case 4:
                return "AmstradCPC";
        case 5:
                return "Spectrum";
        case 6:
                return "SamCoupe";
        case 7:
                return "Archimedes";
        case 8:
                return "C64";
        case 9:
                return "Atari8";
        default:
                return "INVALID";
        }
}

/**
 * @brief       CapsImage.dentype to string
 */
static const char *dentype_to_string(uint32_t dentype)
{
        switch(dentype) {
        case 1:
                return "Noise";
        case 2:
                return "Automatic according to track size";
        case 3:
                return "Amiga CopyLock old";
        case 4:
                return "Amiga CopyLock";
        case 5:
                return "AtariST CopyLock";
        case 6:
                return "Amiga SpeedLock";
        case 7:
                return "Amiga SpeedLock Old";
        case 8:
                return "Adam Brierley Amiga"; 
        case 9:
                return "Adam Brierley Amiga2";
        default:
                return "INVALID";
        }
}

static const char *sampletype_to_string(uint32_t sampletype)
{
        switch(sampletype) {
        case 0:
                // data stream end
                return "cpdatEnd";
        case 1:
                // mark/sync
                return "cpdatMark";
        case 2:
                // data
                return "cpdatData";
        case 3:
                // gap
                return "cpdatGap";
        case 4:
                // raw
                return "cpdatRaw";
        case 5:
                // flakey data
                return "cpdatFData";
        case 6:
                return "cpdatLast";
        default:
                return "INVALID";
        }
}

static void hexdump(void *rdata, size_t len)
{
        uint8_t *data = rdata;
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

