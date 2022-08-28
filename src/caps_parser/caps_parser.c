#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <endian.h>

#include "caps_parser.h"

struct caps_parser *caps_parser_init(FILE *fp)
{
        static_assert(sizeof(struct caps_header) == 12, "Size assertion failed!");

        struct caps_parser *p = malloc(sizeof(*p));
        if (!p) {
                return NULL;
        }
        p->fp = fp;
        p->caps_list_head.next = NULL;
        p->caps_list_head.fpos = ftell(p->fp);

        int rc = fread(&p->caps_list_head.header, sizeof p->caps_list_head.header, 1, p->fp);
        if (rc != 1) {
                return NULL;
        }
        p->caps_list_head.header.name = htobe32(p->caps_list_head.header.name);
        p->caps_list_head.header.len = be32toh(p->caps_list_head.header.len);

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
                n->fpos = ftell(p->fp);

                rc = fread(&n->header, sizeof n->header, 1, p->fp);
                if (rc != 1) {
                        free(n);
                        break;
                }

                n->header.name = htobe32(n->header.name);
                n->header.len = be32toh(n->header.len);

                fseek(p->fp, n->header.len, SEEK_CUR);

                node->next = n;
                node = node->next;
        }

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

        printf("Found INFO chunk! at file offset: %ld\n", node->fpos);
        if (node->header.len != 96) {
                printf("Expected 96 bytes in INFO, got %d bytes!\n", node->header.len);
        }
        return;
}
