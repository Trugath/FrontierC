/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_ASSET_TREE_H
#define FE2_ASSET_TREE_H

#include "asset_archive.h"

#include <stddef.h>

typedef struct fe2_tree_archive {
    fe2_asset_part *parts;
    char **names;
    uint8_t **blobs;
    size_t count;
} fe2_tree_archive;

int fe2_pack_tree(const char *data_root, fe2_tree_archive *tree);
void fe2_tree_free(fe2_tree_archive *tree);

#endif
