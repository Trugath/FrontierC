/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_DENSITY_GRID_H
#define FE2_DENSITY_GRID_H
#include <stdint.h>
typedef struct fe2_density_samples { uint8_t a,b,c,d; } fe2_density_samples;
/* Native immutable galaxy resource; horizontal coordinate first. */
int fe2_density_neighbors(uint16_t horizontal,uint16_t vertical,fe2_density_samples *);
#endif
