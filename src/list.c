#include <stdlib.h>
#include <string.h>
#include "list.h"

int bb_list_new(struct bb_list **list)
{
        struct bb_list *head = NULL;
        *list = NULL;

        head = malloc(sizeof(*head));
        if (!head) return 0;

        head->next = NULL;
        head->sample_count = 0;
        memset(head->samples, 0x00, 2048 * sizeof(*head->samples));
        *list = head;
        return 1;
}

void bb_list_free(struct bb_list *list)
{
        struct bb_list *next;
        do {
                next = list->next;
                free(list);
                list = next;
        } while (next);
}

int bb_list_append(struct bb_list *list, uint16_t *source, int sample_count)
{
        int rc;
        struct bb_list *item = NULL;
        rc = bb_list_new(&item);
        if (!rc) 
                return 0;

        memcpy(item->samples, source, sample_count * sizeof(*source));
        item->sample_count = sample_count;

        while(list->next)
                list = list->next;
        list->next = item;
        return 1;
}

int bb_list_flatten(struct bb_list *list, uint16_t *dest, int sample_count)
{
        do {
                memcpy(dest, list->samples, list->sample_count * sizeof(*list->samples));
                sample_count -= list->sample_count;
                dest += list->sample_count;
                list = list->next;
        } while (list);

        return sample_count;
}
