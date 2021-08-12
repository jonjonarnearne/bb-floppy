#ifndef FLUX_DATA_H
#define FLUX_DATA_H

#include <stdint.h>

struct flux_data {
        int revolutions;
        int *idx_pos;
        uint32_t *data;
};

struct flux_data *flux_data_init(uint32_t *data, int tot_len, int *idx_pos,
                                                        int revolutions);
void flux_data_free(struct flux_data *data);
#endif /* FLUX_DATA_H */
