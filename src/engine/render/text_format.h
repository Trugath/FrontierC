/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_RENDER_TEXT_FORMAT_H
#define FE2_RENDER_TEXT_FORMAT_H
#include "model.h"

/* Resource directories contain little-endian offsets relative to their base.
 * Views retain the encoded templates; expansion needs no executable image. */
typedef struct fe2_text_directory {
    fe2_render_bytes bytes;
    size_t entries;
} fe2_text_directory;
typedef struct fe2_text_resources {
    fe2_text_directory builtin, external, overlay[64];
    /* Module bank texts (fe2-intro moduleStrings): while a bank is registered,
     * 0x4000..0x7fff resolve here instead of external. Empty = use external. */
    fe2_text_directory module;
    uint16_t overlay_mask[64]; /* selector index mask, normally 0x1ff */
    fe2_render_bytes pointer[2];
    uint32_t pointer_value[2];
    uint32_t value[5], current_day, day_month;
    const uint8_t *widths; /* 256 byte width table; negative resets line */
    /* An overlay's own text entry (+0x28) for 0x8000..0xffff: `slot` the
     * overlay, `doubled` the selector shifted left as the original passes
     * it. Writes a NUL-terminated string and returns 0, or returns nonzero
     * to use the overlay's text directory. Optional. */
    int (*overlay_text)(void *context, const struct fe2_text_resources *resources, unsigned slot,
                        uint16_t doubled, char *out, size_t capacity);
    void *overlay_context;
} fe2_text_resources;

/* Bounded expansion, including recursive selectors, numeric formatting,
 * position controls and wrapping. Returns 0 on success, -1 for bad resources,
 * capacity, recursion or an original arithmetic-domain error. */
int fe2_text_expand(const fe2_text_resources *resources, uint16_t selector,
    uint8_t *output, size_t capacity, size_t *length);
int fe2_text_wrap(uint8_t *text, size_t length, const uint8_t widths[256], unsigned width);
#endif
