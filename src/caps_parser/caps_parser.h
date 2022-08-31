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

struct CapsData {
        uint32_t size;  // data area size in bytes after chunk
        uint32_t bsize; // data area size in bits
        uint32_t dcrc;  // data area crc
        uint32_t did;   // data chunk identifier
};

// original meaning of some CapsBlock entries for old images
struct CapsBlockExt {
        uint32_t blocksize;  // decoded block size, rounded
        uint32_t gapsize;    // decoded gap size, rounded
};

// new meaning of some CapsBlock entries for new images
struct SPSBlockExt {
        uint32_t gapoffset;  // offset of gap stream in data area
        uint32_t celltype;   // bitcell type
};

// union for old or new images
union CapsBlockType {
        struct CapsBlockExt caps; // access old image
        struct SPSBlockExt sps;   // access new image
};

// block image descriptor
struct CapsBlock {
        uint32_t blockbits;  // decoded block size in bits
        uint32_t gapbits;    // decoded gap size in bits
        union CapsBlockType bt;  // content depending on image type
        uint32_t enctype;    // encoder type
        uint32_t flag;       // block flags
        uint32_t gapvalue;   // default gap value
        uint32_t dataoffset; // offset of data stream in data area
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

bool caps_parser_get_caps_image_for_did(struct caps_parser *p,
                                        struct CapsImage *caps_image,
                                        uint32_t did);

void caps_parser_show_file_info(struct caps_parser *p);
void caps_parser_show_track_info(struct caps_parser *p, unsigned int cylinder,
                                                        unsigned char head);
void caps_parser_show_data(struct caps_parser *p, uint32_t did);
#endif /* CAPS_PARSER_H */


