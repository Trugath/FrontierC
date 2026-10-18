/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "video.h"
#include <string.h>
static int signed16(unsigned n) { n &= 65535u; return n < 32768u ? (int)n : (int)n - 65536; }

static void reset(fe2_graphics_cache *c)
{
    unsigned i;
    for (i = 0; i < FE2_GRAPHICS_RECORDS; ++i) c->offset[i] = 65535;
    c->head = c->tail = c->live = c->cursor = 0;
    c->limit = FE2_GRAPHICS_ARENA_BYTES; c->high_water = 1;
}
void fe2_graphics_cache_init(fe2_graphics_cache *c)
{
    if (c) { memset(c, 0, sizeof(*c)); reset(c); }
}
int fe2_graphics_cache_bind(fe2_graphics_cache *c, unsigned id, fe2_render_bytes record)
{
    if (!c || id >= FE2_GRAPHICS_RECORDS || !record.data || record.count < 8 ||
        record.count > FE2_GRAPHICS_ARENA_BYTES - 2) return -1;
    c->source[id] = record; reset(c); return 0;
}
int fe2_graphics_cache_get(fe2_graphics_cache *c, unsigned id, fe2_render_bytes *record)
{
    size_t size;
    unsigned at;
    if (!c || !record || id >= FE2_GRAPHICS_RECORDS || !c->source[id].data) return -1;
    size = c->source[id].count + 2;
    if (size > FE2_GRAPHICS_ARENA_BYTES || size < 10) return -1;
    if (c->offset[id] == 65535) {
        while (c->live >= FE2_GRAPHICS_RECORDS || c->cursor + size > c->limit ||
            c->cursor + size > FE2_GRAPHICS_ARENA_BYTES) {
            unsigned evicted, end, bytes;
            if (!c->live) { c->cursor = 0; c->limit = FE2_GRAPHICS_ARENA_BYTES; c->high_water = 1; break; }
            evicted = c->fifo[c->head]; c->head = (c->head + 1) % FE2_GRAPHICS_RECORDS;
            at = c->offset[evicted] - 2u; c->offset[evicted] = 65535; --c->live;
            bytes = (unsigned)c->arena[at] + (unsigned)c->arena[at + 1] * 256u;
            end = at + bytes;
            if (end == c->high_water) { c->limit = FE2_GRAPHICS_ARENA_BYTES; c->high_water = 1; }
            else if (!at) { c->cursor = 0; c->limit = bytes; }
            else c->limit += bytes;
        }
        at = c->cursor; c->cursor += (unsigned)size;
        c->arena[at] = (uint8_t)size; c->arena[at + 1] = (uint8_t)(size >> 8);
        memcpy(c->arena + at + 2, c->source[id].data, size - 2);
        c->offset[id] = (uint16_t)(at + 2);
        c->fifo[c->tail] = (uint16_t)id; c->tail = (c->tail + 1) % FE2_GRAPHICS_RECORDS; ++c->live;
        if (c->cursor >= c->high_water) c->high_water = c->cursor;
    }
    record->data = c->arena + c->offset[id]; record->count = size - 2;
    return 0;
}
int fe2_graphics_decode(fe2_render_bytes r, uint8_t *pixels, size_t capacity, fe2_render_texture_image *image)
{
    size_t size, at = 8, out = 0;
    unsigned width, height;
    if (!r.data || r.count < 8 || !pixels || !image || r.data[0] == 255) return -1;
    width = (unsigned)r.data[2] + (unsigned)r.data[3] * 256u; height = r.data[1];
    size = (size_t)width * height;
    if (!width || !height || capacity < size) return -1;
    if (!(r.data[0] & 2u)) {
        if (r.count - at < size) return -1;
        memcpy(pixels, r.data + at, size);
    } else {
        while (out < size) {
            unsigned token, count;
            if (at >= r.count) return -1;
            token = r.data[at++];
            if (token < 128) {
                count = token;
                if (!count || count > size - out || count > r.count - at) return -1;
                memcpy(pixels + out, r.data + at, count); out += count; at += count;
            } else {
                unsigned distance;
                count = (256u - token + 7u) / 8u;
                if (at >= r.count) return -1;
                distance = (token & 7u) * 256u + r.data[at++];
                if (!distance || distance > out || count > size - out) return -1;
                while (count--) { pixels[out] = pixels[out - distance]; ++out; }
            }
        }
    }
    image->pixels = pixels; image->bytes = size; image->width = width; image->height = height;
    return 0;
}
void fe2_video_init(fe2_video *v) { if (v) memset(v, 0, sizeof(*v)); }
void fe2_video_span(void *context, unsigned y, unsigned x, unsigned count, uint8_t color)
{
    fe2_video *v = context;
    unsigned i;
    if (!v || v->drawing > 1) return;
    for (i = 0; i < (count ? count : 1u); ++i) v->pages[v->drawing][(uint16_t)(y * 320u + x + i)] = color;
}
void fe2_video_pixel(fe2_video *v, uint16_t x, uint8_t y, uint8_t color) { fe2_video_span(v, y, x, 0, color); }
int fe2_video_rectangle(fe2_video *v, uint16_t left, uint16_t right, uint8_t top, uint8_t bottom,
    uint8_t color, int preserve)
{
    unsigned x, y, width = (uint16_t)(right - left + 1u), unit;
    if (!v || v->drawing > 1 || !width || right < left || bottom < top) return -1;
    unit = preserve || (width & 1u) ? 1u : width & 2u ? 2u : 4u;
    for (y = top; y <= bottom; ++y) for (x = 0; x < width; x += unit) {
        unsigned at = (uint16_t)(y * 320u + left + x), j;
        /* REP word/dword stores wrap DI between stores, not inside one.
         * Bytes beyond the page belong to the original adjacent segment. */
        for (j = 0; j < unit && at + j < FE2_VIDEO_PAGE_BYTES; ++j)
            if (!preserve || (v->pages[v->drawing][at + j] & 252u) != 40u)
                v->pages[v->drawing][at + j] = color;
    }
    return 0;
}
static void marker_rectangle(fe2_video *v, uint16_t left, uint8_t top, uint8_t bottom, uint8_t color, int clip,
                             int preserve)
{
    if (top < 168 && clip) top = 168;
    if (bottom > 198) bottom = 198;
    if (top <= bottom) (void)fe2_video_rectangle(v, left, (uint16_t)(left + 1u), top, bottom, color, preserve);
}
void fe2_video_scanner_marker(fe2_video *v, uint16_t x, uint8_t base, int16_t height, uint8_t color, int clip)
{
    uint8_t h = (uint8_t)height;
    uint16_t stem = (uint16_t)(x - 1u), cap = (uint16_t)(x + 1u);
    if (height >= 0) {
        marker_rectangle(v, stem, (uint8_t)(base - h - 1u), base, color, clip, 0);
        marker_rectangle(v, cap, (uint8_t)(base - h - 1u), (uint8_t)(base - h), color, clip, 0);
    } else {
        marker_rectangle(v, stem, base, (uint8_t)(base - h + 1u), color, clip, 1);
        marker_rectangle(v, cap, (uint8_t)(base - h), (uint8_t)(base - h + 1u), color, clip, 1);
    }
}
void fe2_video_clear_view(fe2_video *v, uint8_t color)
{
    if (v && v->drawing < 2) memset(v->pages[v->drawing], color, 320u * 168u);
}
void fe2_video_copy_frame(fe2_video *v)
{
    if (v && v->drawing < 2) memcpy(v->pages[v->drawing], v->pages[v->drawing ^ 1u], FE2_VIDEO_FRAME_BYTES);
}
void fe2_video_save_panel(fe2_video *v)
{
    if (v && v->drawing < 2) memcpy(v->pages[v->drawing ^ 1u] + 320u * 168u,
        v->pages[v->drawing] + 320u * 168u, 320u * 32u);
}
void fe2_video_palette(void *context, uint8_t index, uint8_t red, uint8_t green, uint8_t blue)
{
    fe2_video *v = context;
    if (v) { v->palette[index][0] = red & 63u; v->palette[index][1] = green & 63u; v->palette[index][2] = blue & 63u; }
}
int fe2_video_blit(fe2_video *v, fe2_render_bytes record, int x, int y, uint8_t *workspace, size_t capacity)
{
    fe2_render_texture_image image;
    unsigned row, col;
    int origin_x, origin_y;
    if (!v || v->drawing > 1 || fe2_graphics_decode(record, workspace, capacity, &image)) return -1;
    origin_x = (int)((unsigned)record.data[4] + (unsigned)record.data[5] * 256u);
    origin_y = (int)((unsigned)record.data[6] + (unsigned)record.data[7] * 256u);
    if (origin_x >= 32768) origin_x -= 65536;
    if (origin_y >= 32768) origin_y -= 65536;
    x = signed16((unsigned)(uint16_t)x + (unsigned)(uint16_t)origin_x);
    y = signed16((unsigned)(uint16_t)y + (unsigned)(uint16_t)origin_y);
    for (row = 0; row < image.height; ++row) for (col = 0; col < image.width; ++col) {
        uint8_t pixel = workspace[(size_t)row * image.width + col];
        int dx = x + (int)col, dy = y + (int)row;
        if (dx >= 0 && dx < 320 && dy >= 0 && dy < 200 && (pixel || !(record.data[0] & 1u)))
            v->pages[v->drawing][dy * 320 + dx] = pixel;
    }
    return 0;
}
int fe2_video_present(fe2_video *v, fe2_video_present_fn present, void *context)
{
    if (!v || v->drawing > 1 || !present) return -1;
    v->presented_ticks = v->ticks; v->ticks = 0;
    present(context, v->pages[v->drawing], &v->palette[0][0]);
    return 0;
}
int fe2_video_save_pointer(fe2_video *v, fe2_render_bytes sprite, int16_t x, int16_t y,
    fe2_video_saved_region *saved)
{
    int left, top, width, height;
    unsigned row;
    if (!v || v->drawing > 1 || !saved || !sprite.data || sprite.count < 8) return -1;
    saved->valid = 0;
    width = (int)((unsigned)sprite.data[2] + (unsigned)sprite.data[3] * 256u); height = sprite.data[1];
    if (width > 320) return -1;
    left = signed16((unsigned)(uint16_t)x + (unsigned)sprite.data[4] + (unsigned)sprite.data[5] * 256u);
    left = signed16((unsigned)left & 65532u);
    top = signed16((unsigned)(uint16_t)y + (unsigned)sprite.data[6] + (unsigned)sprite.data[7] * 256u);
    if (top < 0) { height += top; top = 0; }
    if (top + height > 200) height = 200 - top;
    if (left < 0) { width += left; left = 0; }
    if (left + width > 320) width = 320 - left;
    if (width <= 0 || height <= 0) return 0;
    width = ((left + width + 7) & ~3) - left;
    saved->x = (unsigned)left; saved->y = (unsigned)top;
    saved->width = (unsigned)width; saved->height = (unsigned)height;
    for (row = 0; row < saved->height; ++row)
        memcpy(saved->pixels + (size_t)row * saved->width,
            v->pages[v->drawing] + (saved->y + row) * 320u + saved->x, saved->width);
    saved->valid = 1; return 1;
}
int fe2_video_restore_pointer(fe2_video *v, fe2_video_saved_region *saved)
{
    unsigned row;
    if (!v || v->drawing > 1 || !saved) return -1;
    if (!saved->valid) return 0;
    if (saved->x > 319 || saved->y > 199 || saved->height > 200 - saved->y ||
        saved->width > 324 - saved->x) return -1;
    for (row = 0; row < saved->height; ++row)
        memcpy(v->pages[v->drawing] + (saved->y + row) * 320u + saved->x,
            saved->pixels + (size_t)row * saved->width, saved->width);
    saved->valid = 0; return 1;
}
int fe2_video_present_pointer(fe2_video *v, fe2_render_bytes sprite, int16_t x, int16_t y,
    fe2_video_saved_region *saved, uint8_t *workspace, size_t capacity, fe2_video_present_fn present, void *context)
{
    int status, restore;
    if (!present) return -1;
    status = fe2_video_save_pointer(v, sprite, x, y, saved);
    if (status < 0) return -1;
    status = fe2_video_blit(v, sprite, x, y, workspace, capacity);
    if (!status) status = fe2_video_present(v, present, context);
    restore = fe2_video_restore_pointer(v, saved);
    return status < 0 || restore < 0 ? -1 : 0;
}
int fe2_video_character_metric(fe2_render_bytes metrics, uint8_t character, uint8_t *metric)
{
    size_t at = (size_t)character * 11u;
    if (!metrics.data || !metric || at >= metrics.count) return -1;
    *metric = metrics.data[at]; return 0;
}
int fe2_video_text_metric(fe2_render_bytes metrics, fe2_render_bytes text, int16_t *metric)
{
    unsigned sum = 0;
    size_t i;
    if (!metric || !text.data) return -1;
    for (i = 0; i < text.count; ++i) {
        uint8_t character = text.data[i], value;
        if (!character) { *metric = (int16_t)signed16(sum); return 0; }
        character = (uint8_t)(character - 32u);
        if (character & 128u) continue;
        if (fe2_video_character_metric(metrics, character, &value)) return -1;
        sum = (sum + (value < 128 ? value : (unsigned)((int)value - 256))) & 65535u;
    }
    return -1;
}
int fe2_video_transformed_bitmap(fe2_video *v, fe2_render_bytes sprite, const fe2_screen_point points[3],
    uint16_t selector, const uint8_t palette[256], uint8_t *workspace, size_t capacity)
{
    fe2_render_texture_image image;
    uint8_t identity[256];
    unsigned i;
    if (!v || v->drawing > 1 || fe2_graphics_decode(sprite, workspace, capacity, &image)) return -1;
    if (!palette) { for (i = 0; i < 256; ++i) identity[i] = (uint8_t)i; palette = identity; }
    return fe2_render_draw_texture_image(points, 3, selector, &image, palette, fe2_video_span, v);
}

int fe2_video_draw_text_both(fe2_video *v, const uint8_t *font, size_t glyphs, const uint8_t *color_map,
                             const uint8_t *text, size_t length, fe2_render_text_state *state)
{
    if (!v || v->drawing > 1) return -1;
    return fe2_render_draw_text_both(v->pages[v->drawing], v->pages[v->drawing ^ 1u], font, glyphs, color_map,
                                     text, length, state);
}
