/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_ASSET_IMAGE_H
#define FE2_ASSET_IMAGE_H

#include "asset_util.h"

#include <stdint.h>

typedef struct fe2_image {
    unsigned width, height;
    uint8_t *pixels;
} fe2_image;

uint32_t fe2_crc32(const uint8_t *data, size_t count);
int fe2_read_png(const char *path, fe2_image *image);
void fe2_image_free(fe2_image *image);
int fe2_compress_sprite(const uint8_t *data, size_t count, fe2_buf *out);

typedef struct fe2_sprite {
    unsigned flags, width, height;
    int offset_x, offset_y;
    uint8_t *pixels;
    char *plan;
} fe2_sprite;

int fe2_decode_grf(const uint8_t *data, size_t count, fe2_sprite **sprites, size_t *sprite_count);
void fe2_sprites_free(fe2_sprite *sprites, size_t count);
int fe2_write_indexed_png(const char *path, unsigned width, unsigned height, const uint8_t *pixels,
                          int transparent_zero, const uint8_t *rgb6, size_t colors);

#endif
