#include <assert.h>
#include <endian.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "caps_parser.h"

static const char __attribute__((__unused__)) * platform_to_string(
                                                        uint32_t platform_id);
static const char __attribute__((__unused__)) * dentype_to_string(
                                                        uint32_t dentype);
static void __attribute__((__unused__)) print_caps_image(
                                                struct CapsImage *caps_image);
static bool __attribute__((__unused__)) caps_parser_read_caps_image_node(
                                struct caps_parser *p, struct caps_node *node,
                                                struct CapsImage *caps_image);
static bool __attribute__((__unused__)) caps_parser_read_caps_data_node(
                                struct caps_parser *p, struct caps_node *node,
                                                struct CapsData *caps_data);
static void __attribute__((__unused__)) print_caps_data(
                                                struct CapsData *caps_data);
static void __attribute__((__unused__)) print_caps_block(
                                                struct CapsBlock *caps_block);

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

        printf("Sample size: %u\n",
                be32toh(node->chunk.data.size) - (sizeof caps_block  * 11));

        /*** READ SAMPLES NOW ***/
        uint8_t sample_head = 0;
        int ret = fread(&sample_head, sizeof sample_head, 1, p->fp);
        if (ret != 1) {
                fprintf(stderr, "ERROR!\n");
                return;
        }

        //uint8_t sample_count_integer_size = sample_head >> 5;
        //uint8_t sample_type = sample_head & 0x1f;

}

static bool caps_parser_read_caps_image_node( struct caps_parser *p,
                        struct caps_node *node, struct CapsImage *caps_image)
{
        int rc = fseek(p->fp, node->fpos, SEEK_SET);
        if (rc != 0) {
                fprintf(stderr,
                        "Failed to seek in disk image, error: %s\n",
                                                        strerror(errno));
                return false;
        }

        rc = fread(caps_image, sizeof(*caps_image), 1, p->fp);
        if (rc != 1) {
                fprintf(stderr, "Failed to read from disk\n");
                return false;              
        }

        return true;
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

static bool caps_parser_read_caps_data_node( struct caps_parser *p,
                        struct caps_node *node, struct CapsData *caps_data)
{
        int rc = fseek(p->fp, node->fpos, SEEK_SET);
        if (rc != 0) {
                fprintf(stderr,
                        "Failed to seek in disk image, error: %s\n",
                                                        strerror(errno));
                return false;
        }

        rc = fread(caps_data, sizeof(*caps_data), 1, p->fp);
        if (rc != 1) {
                fprintf(stderr, "Failed to read from disk\n");
                return false;              
        }

        return true;
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

