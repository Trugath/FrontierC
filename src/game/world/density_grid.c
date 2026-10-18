/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "density_grid.h"
#include "galaxy_density_data.h"
int fe2_density_neighbors(uint16_t horizontal,uint16_t vertical,fe2_density_samples *out)
{
    unsigned at;
    fe2_density_samples s;
    if(!out || !galaxy_density_data || horizontal>=8192 || vertical>=8192) return -1;
    at=(vertical/64u)*128u+horizontal/64u;
    if((size_t)at+130u>galaxy_density_bytes) return -1;
    s.a=galaxy_density_data[at]; s.b=galaxy_density_data[at+1];
    s.c=galaxy_density_data[at+128]; s.d=galaxy_density_data[at+129];
    *out=s;
    return 0;
}
