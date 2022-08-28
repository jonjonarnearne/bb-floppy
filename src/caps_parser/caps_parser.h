#ifndef CAPS_PARSER_H
#define CAPS_PARSER_H

#include <stdio.h>
#include <stdint.h>

struct caps_header {
        uint32_t name;
        uint32_t len;
        uint32_t crc;
};

struct caps_node {
        struct caps_header header;
        long fpos;
        struct caps_node *next;
};

struct caps_parser {
        FILE *fp;
        struct caps_node caps_list_head;
};

struct caps_parser *caps_parser_init(FILE *fp);
void caps_parser_cleanup(struct caps_parser *p);

void caps_parser_show_file_info(struct caps_parser *p);
#endif /* CAPS_PARSER_H */


