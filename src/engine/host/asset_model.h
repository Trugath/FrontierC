/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_ASSET_MODEL_H
#define FE2_ASSET_MODEL_H

#include <stddef.h>
#include <stdint.h>

typedef struct fe2_packed_model {
    char name[64];
    unsigned slot;
    unsigned offset;
    uint8_t *data;
    uint32_t count;
} fe2_packed_model;

typedef struct fe2_bank_image {
    uint8_t *image;
    uint32_t image_count;
    unsigned offsets[240];
    fe2_packed_model *models;
    size_t count;
} fe2_bank_image;

int fe2_compile_bank(const char *ini_path, const char *names_path, fe2_bank_image *bank);
void fe2_bank_free(fe2_bank_image *bank);

#endif
