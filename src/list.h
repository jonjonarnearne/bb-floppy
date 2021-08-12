#ifndef LIST_H
#define LIST_H

#include <stdint.h>

struct bb_list {
        struct bb_list *next;
        int sample_count;
        uint16_t samples[2048];
};
int bb_list_new(struct bb_list **list);
void bb_list_free(struct bb_list *list);
int bb_list_append(struct bb_list *list, uint16_t *source, int sample_count);
int bb_list_flatten(struct bb_list *list, uint16_t *dest, int sample_count);

#endif
