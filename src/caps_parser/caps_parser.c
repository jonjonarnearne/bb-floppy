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
static bool __attribute__((__unused__)) caps_parser_read_caps_image_from_node(
                                struct caps_parser *p, struct caps_node *node,
                                                struct CapsImage *caps_image);

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
                        break;
                }

                n->header.name = htobe32(n->header.name);
                n->header.len = be32toh(n->header.len);

                n->fpos = ftell(p->fp);

                if (n->header.len == 0) {
                        break;
                }
                fseek(p->fp, (n->header.len - sizeof(struct caps_header)), SEEK_CUR);

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

        static_assert(sizeof(struct CapsInfo) == 84, "CapsInfo size assertion failed!");
        if (node->header.len != sizeof(struct CapsInfo) + sizeof(struct caps_header))  {
                printf("Expected %u bytes in INFO, got %d bytes!\n",
                                sizeof(struct CapsInfo) + sizeof(struct caps_header),
                                node->header.len);
                return;
        }

        printf("Found INFO chunk! at file offset: %ld\n", node->fpos);
        struct CapsInfo capsinfo;
        int rc = fseek(p->fp, node->fpos, SEEK_SET);
        if (rc != 0) {
                fprintf(stderr, "Failed to seek in file, error: %s\n",
                                                        strerror(errno));
                return;
        }
        rc = fread(&capsinfo, sizeof capsinfo, 1, p->fp);
        if (rc != 1) {
                fprintf(stderr, "Failed to read capsinfo block from file!\n");
                return;
        }
        printf("*------------------------------------------------------------------------\n");
        printf("|                                CapsInfo:\n");
        printf("*------------------------------------------------------------------------\n");
        printf("| Image type: %s\n", be32toh(capsinfo.type) == 1 ? "FDD" :
                                   be32toh(capsinfo.type) == 0 ? "N/A" :
                                                 "UNKNOWN VERSION");
        printf("| Encoder: %s\n", be32toh(capsinfo.encoder) == 2 ? "RAW" :
                                be32toh(capsinfo.encoder) == 1 ? "MFM" :
                                be32toh(capsinfo.encoder) == 0 ? "N/A" :
                                                 "UNKNOWN VERSION");
        printf("| Encoder revision: %d\n", be32toh(capsinfo.encrev));
        printf("| Release: %d\n", be32toh(capsinfo.release));
        printf("| Revision: %d\n", be32toh(capsinfo.revision));
        printf("| Original source ref.: 0x%08x\n", be32toh(capsinfo.origin));
        printf("| Min. cylinder: %d\n", be32toh(capsinfo.mincylinder));
        printf("| Max. cylinder: %d\n", be32toh(capsinfo.maxcylinder));
        printf("| Min. head: %d\n", be32toh(capsinfo.minhead));
        printf("| Max. head: %d\n", be32toh(capsinfo.maxhead));
        printf("| Date: 0x%08x (%u)\n", be32toh(capsinfo.date),
                                      be32toh(capsinfo.date));
        printf("| Time: 0x%08x (%u)\n", be32toh(capsinfo.time),
                                      be32toh(capsinfo.time));
        printf("| Platforms: [");
        for (unsigned int i = 0; i < 4; ++i) {
                const uint32_t id = be32toh(capsinfo.platform[i]);
                if (id == 0 || id > 9) {
                        // Platform is N/A. or above max.
                        continue;
                }
                if (i > 0) {
                        printf(", ");
                }
                printf("%s", platform_to_string(be32toh(capsinfo.platform[i])));
        }
        printf("]\n");
        printf("| Disk #: %d\n", be32toh(capsinfo.disknum));
        printf("| User Id: 0x%08x\n", be32toh(capsinfo.userid));
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
        static_assert(sizeof(struct CapsImage) == 68,
                                        "CapsImage size assertion failed!");
        static const size_t expected_len = sizeof(struct CapsImage)
                                         + sizeof(struct caps_header);

        unsigned int count = 0;
        struct CapsImage caps_image;
        struct caps_node *node = p->caps_list_head.next;
        while(node) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmultichar"
                if (node->header.name == 'IMGE') {
#pragma GCC diagnostic pop
                        if (node->header.len != expected_len)  {
                                fprintf(stderr,
                                        "Bad IMGE size, expected %u bytes, got %u bytes!\n",
                                        expected_len, node->header.len);
                                count++;
                                goto next_node;
                        }
                        bool rc = caps_parser_read_caps_image_from_node(
                                                        p, node, &caps_image);
                        if (!rc) {
                                fprintf(stderr, "Failed to read at node: %u\n",
                                                                        count);
                                break;
                        }
                        if (be32toh(caps_image.cylinder) != cylinder ||
                            be32toh(caps_image.head) != head) {
                                // Keep looking
                                count++;
                                goto next_node;
                        }

                        // Done!
                        print_caps_image(&caps_image);
                        return;
                }
next_node:
                node = node->next;
        }

        // We only drop out of this loop if we dindn't find what we was looking for!
        fprintf(stderr, "Could not find IMGE data for cyl: %u head: %u\n",
                                                        cylinder, head);
}

static bool caps_parser_read_caps_image_from_node( struct caps_parser *p,
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

