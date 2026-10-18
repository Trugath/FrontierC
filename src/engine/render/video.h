/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_RENDER_VIDEO_H
#define FE2_RENDER_VIDEO_H
#include "backend.h"
enum { FE2_VIDEO_PAGE_BYTES = 65536, FE2_VIDEO_FRAME_BYTES = 64000,
    FE2_GRAPHICS_RECORDS = 225, FE2_GRAPHICS_ARENA_BYTES = 61440 };
typedef struct fe2_graphics_cache {
    uint8_t arena[FE2_GRAPHICS_ARENA_BYTES];
    uint16_t offset[FE2_GRAPHICS_RECORDS], fifo[FE2_GRAPHICS_RECORDS];
    unsigned head, tail, live, cursor, limit, high_water;
    fe2_render_bytes source[FE2_GRAPHICS_RECORDS]; /* eight-byte header + payload */
} fe2_graphics_cache;
void fe2_graphics_cache_init(fe2_graphics_cache *cache);
/* Resources must outlive the cache. Rebinding a record resets cached entries.
 * Views returned by get survive until the next miss or rebind. */
int fe2_graphics_cache_bind(fe2_graphics_cache *cache, unsigned id, fe2_render_bytes record);
int fe2_graphics_cache_get(fe2_graphics_cache *cache, unsigned id, fe2_render_bytes *record);
int fe2_graphics_decode(fe2_render_bytes record, uint8_t *pixels, size_t capacity,
    fe2_render_texture_image *image);

typedef struct fe2_video {
    uint8_t pages[2][FE2_VIDEO_PAGE_BYTES];
    uint8_t palette[256][3]; /* six-bit DAC components */
    unsigned drawing;
    uint16_t ticks, presented_ticks;
} fe2_video;
void fe2_video_init(fe2_video *video);
void fe2_video_span(void *video, unsigned y, unsigned x, unsigned count, uint8_t color);
void fe2_video_pixel(fe2_video *video, uint16_t x, uint8_t y, uint8_t color);
int fe2_video_rectangle(fe2_video *video, uint16_t left, uint16_t right,
    uint8_t top, uint8_t bottom, uint8_t color, int preserve_group28);
/* Main 0xde8b: a scanner marker at column x from base row `base`: a stem
 * (x-1..x) and a cap (x+1..x+2). A nonnegative height draws upward,
 * overwriting; a negative one downward, preserving palette group 0x28..0x2b.
 * Row arithmetic wraps modulo 256. Rows clamp to the panel from row 168
 * (when clip is enabled) and to row 198 below; there is no column clipping. */
void fe2_video_scanner_marker(fe2_video *video, uint16_t x, uint8_t base, int16_t height, uint8_t color, int clip);
void fe2_video_clear_view(fe2_video *video, uint8_t color);
/* controlled text on both pages (fe2_render_draw_text_both). */
int fe2_video_draw_text_both(fe2_video *video, const uint8_t *font, size_t glyphs, const uint8_t *color_map,
                             const uint8_t *text, size_t length, fe2_render_text_state *state);
void fe2_video_copy_frame(fe2_video *video); /* second page -> drawing page */
void fe2_video_save_panel(fe2_video *video); /* drawing page -> second, rows 168..199 */
void fe2_video_palette(void *video, uint8_t index, uint8_t red, uint8_t green, uint8_t blue);
int fe2_video_blit(fe2_video *video, fe2_render_bytes record, int x, int y,
    uint8_t *workspace, size_t capacity);
typedef void (*fe2_video_present_fn)(void *context, const uint8_t *pixels, const uint8_t *rgb6);
int fe2_video_present(fe2_video *video, fe2_video_present_fn present, void *context);
typedef struct fe2_video_saved_region {
    uint8_t pixels[FE2_VIDEO_PAGE_BYTES];
    unsigned x, y, width, height;
    int valid;
} fe2_video_saved_region;
/* Original pointer capture aligns X down to four, then rounds the clipped
 * right edge with +7, retaining the extra dword past an aligned boundary. */
int fe2_video_save_pointer(fe2_video *video, fe2_render_bytes sprite, int16_t x, int16_t y,
    fe2_video_saved_region *saved);
int fe2_video_restore_pointer(fe2_video *video, fe2_video_saved_region *saved);
int fe2_video_present_pointer(fe2_video *video, fe2_render_bytes sprite, int16_t x, int16_t y,
    fe2_video_saved_region *saved, uint8_t *workspace, size_t capacity,
    fe2_video_present_fn present, void *context);
/* Metric tables are bounded views beginning at the first metric byte, with
 * the recovered stride eleven (distinct from drawable ten-byte glyphs). */
int fe2_video_character_metric(fe2_render_bytes metrics, uint8_t character, uint8_t *metric);
int fe2_video_text_metric(fe2_render_bytes metrics, fe2_render_bytes text, int16_t *metric);
/* The transformed bitmap service is the affine triangle renderer; palette
 * indices can pass through unchanged or use a supplied 256-entry mapping. */
int fe2_video_transformed_bitmap(fe2_video *video, fe2_render_bytes sprite,
    const fe2_screen_point points[3], uint16_t selector, const uint8_t palette[256],
    uint8_t *workspace, size_t capacity);
#endif
