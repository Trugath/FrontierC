/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_ASSET_SEED_H
#define FE2_ASSET_SEED_H

#include <stddef.h>
#include <stdint.h>

typedef struct fe2_authored {
    const char *path;
    const uint8_t *data;
    unsigned count;
} fe2_authored;

extern const fe2_authored fe2_authored_files[];
extern const size_t fe2_authored_count;

#endif
