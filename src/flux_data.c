#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "flux_data.h"

/* Cross correlation - get the delay for the consecutive revolutions */
/* http://paulbourke.net/miscellaneous/correlate/ */
static int xcorr(uint32_t *x, uint32_t *y, int len, double mx, double sx)
{
        int i, j, delay, best_delay;
        double my, sy, sxy, denom, r;

        // Calculate Y_Mean
        my = 0;
        for (i = 0; i < len; i++)
                my += y[i];
        my /= len;
        //printf("mean x: %f, mean y: %f\n", mx, my);

        // Calculate Y_Denominator
        sy = 0;
        for (i = 0; i < len; i++)
                sy += (y[i] - my) * (y[i] - my);
        denom = sqrt(sx * sy);
        //printf("denominator: %f\n", denom);

        r = 0;
        best_delay = 0;
        for (delay = -10; delay < 10; delay++) {
                sxy = 0;
                for (i = 0; i < len; i++) {
                        j = i + delay;
                        if (j < 0 || j >= len)
                                continue;
                        sxy += (x[i] - mx) * (y[j] - my);
                }
                //printf("Delay: %3d - coefficient: %8f\n", delay, sxy / denom);
                if (sxy / denom > r) {
                        r = sxy / denom;
                        best_delay = delay;
                }
        }
        return best_delay;
}

struct flux_data *flux_data_init(uint32_t *data, int tot_len, int *idx_pos,
                                                        int revolutions)
{
        int i, len, delay;
        uint32_t *x, *y;
        double mx, sx;
        struct flux_data *flux_data = NULL;

        flux_data = malloc(sizeof(*flux_data));
        if (!flux_data)
                return NULL;

        flux_data->revolutions = revolutions;

        flux_data->data = calloc(sizeof(*flux_data->data), tot_len);
        if (!flux_data->data)
                goto err_out;
        memcpy(flux_data->data, data, sizeof(*data) * tot_len);

        flux_data->idx_pos = calloc(sizeof(*flux_data->idx_pos), revolutions);
        if (!flux_data->idx_pos)
                goto err_out;

        // Make sure len is within range
        len = idx_pos[0];
        for (i = 1; i < revolutions; i++) {
                len = (idx_pos[i] > len) ? len : idx_pos[i];
        }

        x = data;
        y = data;

        // Calculate X_Mean
        mx = 0;
        for (i = 0; i < len; i++)
                mx += x[i];
        mx /= len;

        // Calculate denominator
        sx = 0;
        for (i = 0; i < len; i++)
                sx += (x[i] - mx) * (x[i] - mx);

        // We use this loop both to cross corrolate the offsets,
        // and to fill the idx_pos of flux_data, wrt. the cross corrolation
        delay = 0;
        for (i = 0; i < revolutions; i++) {
                flux_data->idx_pos[i] = idx_pos[i] - delay;

                if (revolutions - 1 == i) break;

                y += idx_pos[i];
                delay = xcorr(x, y, len, mx, sx);
                flux_data->idx_pos[i] += delay;
        }

#if 1
        {
                int rev;
                uint32_t *fd_ptr;

                printf("------------------------------\n"
                       "|  Debug FluxData Alignment  |\n"
                       "------------------------------\n");

                i = 0;
                do { i++; } while(data[i] < 5000);

                printf("Found sample > 5000 @: %d\n", i);
                fd_ptr = data;
                for (rev = 0; rev < revolutions; rev++) {
                        printf("\n%d\n", rev);
                        printf("[%d %d %d %d]\n", fd_ptr[i-4], fd_ptr[i-3],
                                                  fd_ptr[i-2], fd_ptr[i-1]);
                        printf("[%d %d %d %d]\n", fd_ptr[i],   fd_ptr[i+1],
                                                  fd_ptr[i+2], fd_ptr[i+3]);
                        fd_ptr += flux_data->idx_pos[rev];
                }
        }
#endif
        return flux_data;

err_out:
        if (flux_data) flux_data_free(flux_data);

        return NULL;
}

void flux_data_free(struct flux_data *flux_data)
{
        if (flux_data->data)
                free(flux_data->data);
        if (flux_data->idx_pos)
                free(flux_data->idx_pos);
        free(flux_data);
}

