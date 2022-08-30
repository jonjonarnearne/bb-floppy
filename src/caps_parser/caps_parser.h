#ifndef CAPS_PARSER_H
#define CAPS_PARSER_H

#include <stdio.h>
#include <stdint.h>

struct caps_header {
        uint32_t name;
        uint32_t len;
        uint32_t crc;
};

/**
 * Struct stored in INFO header.
 */
struct CapsInfo {
        uint32_t type;          // image type
        uint32_t encoder;       // image encoder ID
        uint32_t encrev;        // image encoder revision
        uint32_t release;       // release ID
        uint32_t revision;      // release revision ID
        uint32_t origin;        // original source reference
        uint32_t mincylinder;   // lowest cylinder number
        uint32_t maxcylinder;   // highest cylinder number
        uint32_t minhead;       // lowest head number
        uint32_t maxhead;       // highest head number
        uint32_t date;
        uint32_t time;
        uint32_t platform[4];   // intended platform(s)
        uint32_t disknum;       // disk# for release, >= 1 if multidisk
        uint32_t userid;        // user id of the image creator
        uint32_t reserved[3];   // future use
};

/**
 * Struct stored in IMGE header.
 */
struct CapsImage {
        uint32_t cylinder;      // cylinder#
        uint32_t head;          // head#
        uint32_t dentype;       // density type
        uint32_t sigtype;       // signal processing type
        uint32_t trksize;       // decoded track size, rounded
        uint32_t startpos;      // start position, rounded
        uint32_t startbit;      // start position on original data
        uint32_t databits;      // decoded data size in bits
        uint32_t gapbits;       // decoded gap size in bits
        uint32_t trkbits;       // decoded track size in bits
        uint32_t blkcnt;        // number of blocks
        uint32_t process;       // encoder prcocess
        uint32_t flag;          // image flags
        uint32_t did;           // data chunk identifier
        uint32_t reserved[3];   // future use
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
void caps_parser_show_track_info(struct caps_parser *p, unsigned int cylinder,
                                                        unsigned char head);
#endif /* CAPS_PARSER_H */


