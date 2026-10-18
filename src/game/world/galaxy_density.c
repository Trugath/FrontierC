/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "galaxy_density.h"
#include "density_grid.h"

int fe2_galaxy_density_neighbors(uint16_t x,uint16_t y,fe2_galaxy_neighbors *out)
{
    fe2_density_samples samples;
    if(!out) return -1;
    if(x>=8192 || y>=8192) return -2;
    fe2_density_neighbors(x,y,&samples);
    *out=(fe2_galaxy_neighbors){samples.a,samples.b,samples.c,samples.d};
    return 0;
}
