/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "texture.h"

static int table_word(int32_t offset, uint16_t *out)
{
    if (offset < 0 || (uint32_t)offset >= fe2_render_material_bytes || fe2_render_material_bytes - (uint32_t)offset < 2)
        return -1;
    *out = (uint16_t)((unsigned)fe2_render_material_data[offset] | (unsigned)fe2_render_material_data[offset + 1] << 8);
    return 0;
}

/* Saturating per-nibble RGB12 addition (bits 12..15 of the entry drop). */
static uint16_t shade(uint16_t color, uint16_t add)
{
    unsigned r = (color & 0xfu) + (add & 0xfu), g = (color & 0xf0u) + (add & 0xf0u),
        b = (color & 0xf00u) + (add & 0xf00u);
    if (r & 0xff0u) r = 0xfu;
    if (g & 0xf00u) g = 0xf0u;
    if (b & 0xf000u) b = 0xf00u;
    return (uint16_t)(r | g | b);
}

static unsigned outcode(fe2_screen_point p)
{
    unsigned code = p.x < 0 ? 1u : p.x >= 320 ? 4u : 2u;
    return code | (p.y < 0 ? 8u : p.y >= 168 ? 0x20u : 0x10u);
}

int fe2_render_build_texture(const fe2_screen_point *cached, unsigned count, uint16_t token,
    uint16_t face, uint32_t inherited, const int32_t camera[3], int16_t scale,
    fe2_render_texture_command *out)
{
    fe2_render_texture_command result = {0};
    uint32_t value = (uint32_t)(token >> 4) & 0xfffu;
    unsigned i, x = 0, y = 0, codes = 0;
    uint16_t add, entry, word;
    if (!cached || !camera || !out || (count != 3 && count != 4)) return -1;
    for (i = 0; i < count; ++i)
        if (cached[i].x < -2000 || cached[i].x > 0x910 || cached[i].y < -2000 || cached[i].y > 0x878)
            return FE2_RENDER_TEXTURE_SOLID;
    if ((value & 0xf00u) == 0xe00u)
        value = (value & 0x10u) | ((value & 0xeu) << 15) | ((value & 0xe0u) << 14);
    if (value & 0x10u) value = (value & ~UINT32_C(0x10)) + inherited;
    if (!(value >> 16)) return FE2_RENDER_TEXTURE_SOLID;
    for (i = 0; i < count; ++i) {
        const fe2_screen_point a = cached[i], b = cached[(i + 1) % count];
        int dx = (int16_t)(uint16_t)(a.x - b.x), dy = (int16_t)(uint16_t)(a.y - b.y);
        x += (unsigned)(dx < 0 ? -dx : dx); y += (unsigned)(dy < 0 ? -dy : dy);
    }
    /* Word sums: the deltas are bounded by the coordinate window. */
    if ((x & 0xffffu) < (count == 3 ? 10u : 14u) && (y & 0xffffu) < (count == 3 ? 10u : 14u))
        return FE2_RENDER_TEXTURE_SOLID;
    for (i = 0; i < count; ++i) codes |= outcode(cached[i]);
    if (!((codes & 2u) || (codes & 5u) == 5u) || !((codes & 0x10u) || (codes & 0x28u) == 0x28u))
        return FE2_RENDER_TEXTURE_REJECTED;
    add = (uint16_t)(face + (uint16_t)value);
    entry = (uint16_t)((value >> 16) << 4);
    if (table_word((int32_t)entry - 16, &result.texture)) return -1;
    for (i = 0; i < 7; ++i) {
        if (table_word((int32_t)entry - 14 + 2 * (int32_t)i, &word)) return -1;
        if (word == 0xffffu) break;
        result.colors[result.color_count++] = shade(word, add);
    }
    result.key = fe2_render_depth_key(camera, scale);
    result.count = count;
    for (i = 0; i < count; ++i) result.points[i] = cached[i];
    *out = result;
    return FE2_RENDER_TEXTURE_EMITTED;
}

void fe2_render_texture_execute(void *context, const void *payload)
{
    fe2_render_drawing_target *target = context;
    if (target && target->texture && payload) target->texture(target->surface_context, payload);
}
