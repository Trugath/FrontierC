/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "backend.h"

/* MCGA bitmap glyphs  and the main controlled text stream
 *. The page is the 64 KiB drawing segment: offsets wrap. */

int fe2_render_draw_glyph(uint8_t *page, const uint8_t *font, size_t glyphs, uint8_t glyph,
                          uint16_t x, uint16_t y, uint8_t color)
{
    const uint8_t *g;
    unsigned row;
    uint16_t offset;
    if (y >= 0xbf) return 0;
    if (!page || !font || glyph >= glyphs) return -1;
    g = font + (size_t)glyph * 10u;
    offset = (uint16_t)(y * 320u + x);
    for (row = 0; row < 9; ++row, offset = (uint16_t)(offset + 320u)) {
        unsigned bits = g[row];
        uint16_t at = offset;
        /* Shift out bits until the remaining byte is zero; set bits store. */
        while (bits) {
            if (bits & 0x80u) page[at] = color;
            bits = (bits << 1) & 0xffu;
            at = (uint16_t)(at + 1u);
        }
    }
    return g[9];
}

static int glyph(uint8_t *page, const uint8_t *font, size_t glyphs, uint8_t value,
                 fe2_render_text_state *s, int shadowed)
{
    int advance;
    if (shadowed && fe2_render_draw_glyph(page, font, glyphs, value, (uint16_t)(s->x + 1u),
                                          (uint16_t)(s->y + 1u), 0) < 0) return -1;
    advance = fe2_render_draw_glyph(page, font, glyphs, value, s->x, s->y, s->color);
    if (advance < 0) return -1;
    s->x = (uint16_t)(s->x + (uint16_t)(int16_t)(int8_t)advance);
    return 0;
}

int fe2_render_draw_text(uint8_t *page, const uint8_t *font, size_t glyphs,
                         const uint8_t *color_map, const uint8_t *text, size_t length,
                         int shadowed, fe2_render_text_state *s)
{
    size_t i = 0;
    if (!s || !text) return -1;
    s->origin_x = s->x;
    if (s->y >= 0xc0) return 0;
    for (;;) {
        uint8_t value;
        if (i >= length) return -1;
        value = text[i++];
        if (!value) return 0;
        value = (uint8_t)(value - 0x20u);
        if (!(value & 0x80u)) {
            if (s->x >= 0x13c) continue;
            if (glyph(page, font, glyphs, value, s, shadowed)) return -1;
        } else if (value == 0xff) {          /* 0x1f: packed X/2, Y */
            if (i + 2 > length) return -1;
            s->x = (uint16_t)(text[i] * 2u);
            s->y = text[i + 1];
            i += 2;
        } else if (value == 0xfe) {          /* 0x1e: X/2 */
            if (i >= length) return -1;
            s->x = (uint16_t)(text[i++] * 2u);
        } else if (value == 0xed) {          /* 0xd: newline */
            s->y = (uint16_t)(s->y + 10u);
            if (!shadowed && s->y > 0xbf) return 0;
            s->x = s->origin_x;
        } else if (value == 0xe1) {          /* 0x1: palette map */
            if (i >= length || !color_map) return -1;
            s->color = color_map[text[i++]];
        }
        /* other controls and 0xa0..0xff are ignored */
    }
}

int fe2_render_draw_positioned_text(uint8_t *page, const uint8_t *font, size_t glyphs,
                                    const uint8_t *color_map, const uint8_t *text, size_t length,
                                    fe2_render_text_state *s)
{
    if (!s || !text || length < 2) return -1;
    s->x = (uint16_t)(text[0] * 2u);
    s->y = text[1];
    s->color = 15;
    return fe2_render_draw_text(page, font, glyphs, color_map, text + 2, length - 2, 0, s);
}

int fe2_render_draw_text_both(uint8_t *page, uint8_t *second, const uint8_t *font, size_t glyphs,
                              const uint8_t *color_map, const uint8_t *text, size_t length,
                              fe2_render_text_state *s)
{
    fe2_render_text_state first;
    int status;
    if (!s) return -1;
    first = *s;
    status = fe2_render_draw_text(second, font, glyphs, color_map, text, length, 0, &first);
    if (status) return status;
    return fe2_render_draw_text(page, font, glyphs, color_map, text, length, 0, s);
}

int fe2_render_draw_label(const fe2_render_text_command *c, const fe2_render_backend *b)
{
    uint8_t buffer[256];
    fe2_render_text_state s;
    if (!c || !b) return -1;
    if (b->text_resources) {
        fe2_text_resources resources = *b->text_resources;
        unsigned i;
        /* Projected labels update the low words of values 2, 3 and 4. */
        for (i = 0; i < 3; ++i)
            resources.value[i + 2] = (resources.value[i + 2] & UINT32_C(0xffff0000)) | c->parameter[i];
        if (fe2_text_expand(&resources, c->selector, buffer, sizeof(buffer), NULL)) return -1;
    } else if (!b->format || b->format(b->context, c->selector, c->parameter, buffer, sizeof(buffer))) return -1;
    s.x = (uint16_t)c->position.x;
    s.y = (uint16_t)c->position.y;
    s.color = 15;
    return fe2_render_draw_text(b->page, b->font, b->font_glyphs, b->color_map, buffer, sizeof(buffer), 0, &s);
}

void fe2_render_backend_text(void *backend, const fe2_render_text_command *command)
{
    (void)fe2_render_draw_label(command, backend);
}
