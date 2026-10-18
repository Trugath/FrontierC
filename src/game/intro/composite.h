/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_COMPOSITE_H
#define FE2_COMPOSITE_H

#include <stdint.h>

typedef struct fe2_composite_video {
    void *opaque;
    int (*rectangle)(void *opaque, uint16_t left, uint16_t right,
                     uint16_t top, uint16_t bottom, uint8_t color);
    int (*palette)(void *opaque, uint8_t start, uint8_t count, const uint8_t *rgb6);
    int (*sprite)(void *opaque, uint16_t resource, int16_t x, int16_t y);
} fe2_composite_video;

typedef struct fe2_composite {
    uint8_t appearance; /* Current composition appearance */
    fe2_composite_video video;
} fe2_composite;

typedef struct fe2_composite_result {
    uint16_t ax;
    uint16_t dx;
} fe2_composite_result;

int fe2_composite_construct(fe2_composite *state, const fe2_composite_video *video);

/* Public 0x746: seven palette uploads, then eight sprite requests; overrides
 * are a captured resident 0x7c0 word. On success AX/DX are exchanged.
 * Returns -1 for invalid arguments or a propagated video-callback error. */
int fe2_composite_draw(fe2_composite *state, uint16_t ax, uint16_t dx,
                       uint16_t override, fe2_composite_result *result);

/* Far style entries 0x969/0xaf1 consume (DX<<16)|AX directly. These do not
 * exchange words, upload palettes, draw overlay parts or change appearance.
 * Styles 0/1 mean A/B. Override bits 1..2 suppress their optional eighth part. */
int fe2_composite_draw_style(fe2_composite *state, unsigned style,
                             uint16_t ax, uint16_t dx, uint16_t override);

/* Recovered local part helpers use an encoded index (four times part number).
 * Explicit bounds reject invalid helper inputs; public masked inputs all fit. */
int fe2_composite_draw_part(fe2_composite *state, unsigned style, uint16_t encoded);
int fe2_composite_draw_overlay(fe2_composite *state, uint16_t encoded);
int fe2_composite_upload_palette(fe2_composite *state, uint16_t native_address,
                                  uint8_t start, uint8_t count);
void fe2_composite_noop(void);

#endif
