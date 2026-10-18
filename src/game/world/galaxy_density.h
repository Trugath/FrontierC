/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_GALAXY_DENSITY_H
#define FE2_GALAXY_DENSITY_H

#include <stdint.h>

enum { FE2_GALAXY_ROW_BYTES=128, FE2_GALAXY_ROWS=129,
       FE2_GALAXY_DATA_BYTES=16512,
       FE2_GALAXY_COORDINATE_LIMIT=8192, FE2_GALAXY_CELL_COORDINATES=64 };

typedef struct fe2_galaxy_neighbors {
    uint8_t first;
    uint8_t right;
    uint8_t below;
    uint8_t below_right;
} fe2_galaxy_neighbors;

/* Bounded chart coordinate domain, backed by the native immutable grid.
 * Preserve flat row addressing at the right edge and the bottom zero tail.
 * Return -1 for null output, -2 for out-of-domain coordinates, else zero.
 * Output is untouched on error. Density weighting is not part of galaxy. */
int fe2_galaxy_density_neighbors(uint16_t x, uint16_t y,
                                  fe2_galaxy_neighbors *output);

#endif
