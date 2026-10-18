/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_RENDER_BACKEND_H
#define FE2_RENDER_BACKEND_H

/*
 * Draw backends: the dynamic palette cache and video primitives of the MCGA
 * driver (el2mcga3) and the main executable's frame-command consumers for
 * round geometry, cubic contours and text. Spans go through the ordinary
 * fe2_render_span_fn contract of pipeline.h (count zero draws one pixel).
 * No registers, guest memory or stack packets: state lives in typed structs,
 * external services are callbacks.
 */
#include <stddef.h>
#include "execution.h"
#include "path.h"
#include "sphere.h"
#include "texture.h"
#include "text_format.h"

/* ---- backend_palette.c: MCGA 0x59ce..0x5d49 ------------------------------- */
enum {
    FE2_PALETTE_SLOTS = 255,      /* slot records 0..254; 0xff is "absent" */
    FE2_PALETTE_FREE_BYTES = 257, /* palette free-list storage */
    FE2_PALETTE_RGB12 = 4096
};
typedef enum fe2_palette_state {
    FE2_PALETTE_RESERVED, FE2_PALETTE_ALIAS, FE2_PALETTE_PENDING, FE2_PALETTE_FREE,
    FE2_PALETTE_REPLACEABLE, FE2_PALETTE_AGING, FE2_PALETTE_RECENT
} fe2_palette_state;
typedef struct fe2_palette_slot {
    uint8_t state, dac;
    uint16_t rgb12;
} fe2_palette_slot;
/* The free list is real byte storage: after fe2_palette_assign it holds
 * (DAC, owner) pairs and a zero terminator, and lookups before the next age
 * pass consume those bytes as slot IDs, as the original does. */
typedef struct fe2_palette_cache {
    fe2_palette_slot slots[FE2_PALETTE_SLOTS];
    uint8_t free_list[FE2_PALETTE_FREE_BYTES];
    uint16_t cursor; /* byte index into free_list */
    uint8_t slot_of[FE2_PALETTE_RGB12];
} fe2_palette_cache;

/* Service 0x23c with AL != 0 : clear, seed slots 0..15 (reserved
 * greys, DAC 0x10..0x1f) and rebuild the free list. */
void fe2_palette_initialize(fe2_palette_cache *cache);
/* Service 0x23c with AL = 0 : release aliases, age recent slots and
 * rebuild the free-slot list. */
void fe2_palette_age(fe2_palette_cache *cache);
/* Service 0x238 : high nibble of the 16-bit word is ignored. Returns
 * the slot (an exhausted free list returns black's slot), or -1 if the
 * cursor has run past the free-list storage (outside the native domain). */
int fe2_palette_lookup(fe2_palette_cache *cache, uint16_t rgb12);
/* Service 0x200 : slot -> DAC index. Slots >= 255 return -1. */
int fe2_palette_index(const fe2_palette_cache *cache, unsigned slot);
/* Service 0x234 : give pending slots free/replaceable DAC entries
 * 0x80..0xff (or alias the nearest lower live color) and upload the new ones in
 * slot order as 6-bit channels nibble*4+3. Returns the number uploaded, or -1
 * if an approximation search cannot terminate (black slot not live). */
typedef void (*fe2_palette_upload_fn)(void *context, uint8_t index, uint8_t red,
                                       uint8_t green, uint8_t blue);
int fe2_palette_assign(fe2_palette_cache *cache, fe2_palette_upload_fn upload, void *context);

/* ---- backend_color.c: main 0xdf28..0xdfff and 0xd9f5..0xda09 --------------- */
typedef struct fe2_render_rgb {
    uint8_t red, green, blue; /* BL, BH, CL */
} fe2_render_rgb;
typedef struct fe2_render_color_services {
    void *context;
    void (*prepare)(void *context, fe2_render_rgb rgb); /* service 0x248 */
    void (*publish)(void *context);                     /* service 0x140 */
    void (*restoring)(void *context);                   /* service 0x1dc */
    /* service 0x1ec: inclusive rectangle in palette index 0 */
    void (*clear)(void *context, uint16_t left, uint16_t right, uint8_t top, uint8_t bottom, uint8_t color);
} fe2_render_color_services;
typedef struct fe2_render_color_state {
    fe2_render_rgb work, current;
    fe2_render_rgb saved, last_127;
    uint8_t restored;
    uint8_t drawing;
    uint16_t work_pointer[2];      /* (service 0x14c) */
} fe2_render_color_state;
/* prepare (service 0x248) only when `prepare` is nonzero. */
void fe2_render_set_color(fe2_render_color_state *state, const fe2_render_color_services *services,
                          int prepare, fe2_render_rgb rgb);
/* (service 0x15c) and 0xdf52 (0x160, without the restoring callback). */
void fe2_render_restore_color(fe2_render_color_state *state, const fe2_render_color_services *services,
                              int callback);
/* saves the supplied color (not the previous one). */
void fe2_render_save_and_set_color(fe2_render_color_state *state,
                                   const fe2_render_color_services *services, fe2_render_rgb rgb);
void fe2_render_enable_drawing(fe2_render_color_state *state);
/* clears 0..319 x 0..199 to index 0. */
void fe2_render_disable_drawing(fe2_render_color_state *state, const fe2_render_color_services *services);
/* write DAC entry 127 only when the current color changed. Returns
 * whether it was uploaded. Channels are passed through unmasked. */
int fe2_render_update_palette_127(fe2_render_color_state *state, fe2_palette_upload_fn upload, void *context);

void fe2_render_set_work_pointer(fe2_render_color_state *state, uint16_t offset, uint16_t segment);
void fe2_render_reset_work_pointer(fe2_render_color_state *state);

/* ---- backend_disc.c: MCGA 0x2dd1..0x3108, main 0x2ca5..0x2d52, 0x2f74 ------- */
/* (video service 0x1d4): overlap tests with half the size, then
 * 0x2f5a. Returns 1 drawn, 0 rejected, -1 for a row count the original would
 * wrap (outside the native domain; nothing drawn). */
int fe2_render_draw_disc_checked(int16_t x, int16_t y, uint16_t size, uint8_t color,
                                 fe2_render_span_fn draw, void *context);
/* (video service 0x1d8): sizes 0..5 fixed patterns with 2 <= x < 318
 * and 2 <= y < 166; 6..1007 octant recurrence; >= 1008 rejected. */
int fe2_render_draw_disc(int16_t x, int16_t y, uint16_t size, uint8_t color,
                         fe2_render_span_fn draw, void *context);
/* (video service 0x214): selector is the byte offset relative to the
 * profile bank origin (bank + 0xe0). Rows are read until a negative byte;
 * -1 if the read leaves the bank (the original reads adjacent code). */
int fe2_render_draw_shape_rows(int16_t anchor, int16_t y, int16_t selector, uint8_t color,
                               fe2_render_span_fn draw, void *context);
enum { FE2_RENDER_SHAPE_PROFILES = 672 };
extern const uint8_t *fe2_render_shape_profiles;
extern const uint8_t (*fe2_render_paired_disc_sizes)[9]; /* outer, inner */

/* Material word (RGB12) -> final palette index, e.g. palette lookup at
 * production followed by fe2_palette_index at drawing time. */
typedef uint8_t (*fe2_render_color_fn)(void *context, uint16_t material);
typedef struct fe2_render_backend {
    fe2_render_color_fn color;
    fe2_render_span_fn span;
    void *context; /* passed to both */
    /* Text formatter (service 0x14): expand selector/parameters into at most
     * capacity bytes including a zero terminator. Return 0 on success. */
    int (*format)(void *context, uint16_t selector, const uint16_t parameter[3],
                  uint8_t *out, size_t capacity);
    const uint8_t *font;     /* ten bytes per glyph: nine rows + advance */
    size_t font_glyphs;
    uint8_t *page;           /* 64 KiB video page addressed with word wrap */
    const uint8_t *color_map; /* text palette map, 256 bytes */
    /* Resolve the low 12-bit graphics-record selector to unpacked pixels. */
    int (*texture)(void *context, unsigned selector, fe2_render_texture_image *image);
    /* Optional 256-entry base texture palette; command colors replace 0..6. */
    const uint8_t *texture_map;
    /* Native selector expansion. When supplied, takes precedence over format. */
    const fe2_text_resources *text_resources;
} fe2_render_backend;

/* Frame callbacks 0x2ca5 (disc), 0x2cc8 (paired disc: outer size map with
 * material[1], then inner with material[0]), 0x2d13 (two shape profiles,
 * selector*32 with material[1], +16 with material[0]) and the 0x2aac..0x2b25
 * chain (two cubic contours filled with material[0], then two with
 * material[1]). Returns 0, or -1 for inputs outside the native domain
 * (paired selector outside 1..9, unreadable profiles, collector overflow or
 * a row with more than six intersections). Earlier spans remain drawn. */
int fe2_render_draw_round(const fe2_render_round_command *command, const fe2_render_backend *backend);
/* unchecked discs of one size and material at signed X/Y pairs. */
int fe2_render_draw_disc_list(uint16_t material, uint16_t size, const fe2_screen_point *points,
                              size_t count, const fe2_render_backend *backend);

/* ---- backend_cubic.c: main 0xb77c..0xbdd4 -------------------------------- */
/* forward-difference workspace for the cubic from `anchor`
 * through p[0..2]. Size bins of the wrapped mean-point Manhattan distance
 * (<4, <0x1e, <0x61a8, else) give 4, 8, 16 or 32 segments. */
typedef struct fe2_render_cubic_steps {
    uint32_t x, y;        /* 16.16 positions (high word is the pixel) */
    uint32_t d1[2], d2[2], d3[2];
    unsigned segments;
} fe2_render_cubic_steps;
void fe2_render_cubic_setup(fe2_screen_point anchor, const fe2_screen_point p[3],
                            fe2_render_cubic_steps *steps);
/* Writes segments + 1 points (the first is the anchor); capacity >= 33. */
unsigned fe2_render_cubic_points(fe2_screen_point anchor, const fe2_screen_point p[3],
                                 fe2_screen_point *out);
/* append each flattened segment to a scanline packet. Returns -1 on
 * collector errors (earlier segments remain). */
int fe2_render_cubic_edges(fe2_render_scanlines *packet, fe2_screen_point anchor,
                           const fe2_screen_point p[3]);
/* flattened segments as lines, drawn only when an endpoint is inside
 * (unsigned) the viewport; the outside endpoint is clipped first. */
void fe2_render_cubic_lines(fe2_screen_point anchor, const fe2_screen_point p[3], uint16_t color,
                            fe2_render_line_fn draw, void *context);

/* ---- backend_text.c: MCGA 0x1a1b..0x1a6a, main 0xe0b6..0xe19d, 0x2f26 ------ */
/* (video service 0x218): glyph byte -> page (offset y*320+x, word
 * wrap). Unsigned y >= 191 draws nothing and returns 0, else the advance.
 * Returns -1 if the glyph is outside the font. */
int fe2_render_draw_glyph(uint8_t *page, const uint8_t *font, size_t glyphs, uint8_t glyph,
                          uint16_t x, uint16_t y, uint8_t color);
/* (and 0xe12b when shadowed): NUL-terminated controlled stream. 0x1f
 * packed XY, 0x1e X byte (doubled), 0xd newline, 0x1 palette map byte.
 * color_map contains 256 entries. Returns 0, or -1 for an unterminated
 * stream or a glyph outside the font. */
typedef struct fe2_render_text_state {
    uint16_t x, y, origin_x; /* start of the current text line */
    uint8_t color;
} fe2_render_text_state;
int fe2_render_draw_text(uint8_t *page, const uint8_t *font, size_t glyphs,
                         const uint8_t *color_map, const uint8_t *text, size_t length,
                         int shadowed, fe2_render_text_state *state);
/* packed position word first (low byte X/2, high byte Y), color 15. */
int fe2_render_draw_positioned_text(uint8_t *page, const uint8_t *font, size_t glyphs,
                                    const uint8_t *color_map, const uint8_t *text, size_t length,
                                    fe2_render_text_state *state);
/* (service 0x104): the stream on the second page, then on the drawing
 * page, both from the same state; the state is the drawing page's pass.
 * The original hides the pointer around it (services 0x18, 0x24); natively
 * the pointer is composited at presentation. */
int fe2_render_draw_text_both(uint8_t *page, uint8_t *second, const uint8_t *font, size_t glyphs,
                              const uint8_t *color_map, const uint8_t *text, size_t length,
                              fe2_render_text_state *state);
/* format into a 256-byte buffer, then draw in color 15 at the
 * label's position (service 0x110). */
int fe2_render_draw_label(const fe2_render_text_command *command, const fe2_render_backend *backend);

/* fe2_render_execution_target round/text callbacks; context is a
 * fe2_render_backend. Domain errors draw nothing further. */
void fe2_render_backend_round(void *backend, const fe2_render_round_command *command);
void fe2_render_backend_text(void *backend, const fe2_render_text_command *command);

/* Filled model paths: straight/cubic edges, intermediate material fills and
 * a final fill. Each intermediate fill starts a fresh scanline packet.
 * Returns -1 for invalid storage/kinds or collector/fill domain errors;
 * completed earlier fills remain drawn. Bind backend_path to target.path. */
int fe2_render_draw_path(const fe2_render_path_command *command, const fe2_render_backend *backend);
void fe2_render_backend_path(void *backend, const fe2_render_path_command *command);

/* Planet/star scanlines (main 0xc99b..0xcc9a, video 0x3343..0x3488).
 * Transition masks are palette byte offsets: multiples of four, 0..60.
 * Zero marks the outline; other masks XOR the active palette selection.
 * Equal-X events retain insertion order. Full rows drop further events,
 * matching the original 31-event limit. Row toggles address visible rows. */
enum { FE2_RENDER_COLOR_EVENTS = 31 };
typedef struct fe2_render_color_event {
    int16_t x;
    uint16_t mask;
} fe2_render_color_event;
typedef struct fe2_render_color_scanlines {
    fe2_render_color_event events[FE2_VIEWPORT_HEIGHT][FE2_RENDER_COLOR_EVENTS];
    uint8_t count[FE2_VIEWPORT_HEIGHT];
    uint16_t toggle[FE2_VIEWPORT_HEIGHT];
    int16_t min_y, max_y;
} fe2_render_color_scanlines;
void fe2_render_color_scanlines_init(fe2_render_color_scanlines *packet);
/* Returns -1 for invalid storage/masks or an unrepresentable X difference. */
int fe2_render_color_edge(fe2_render_color_scanlines *packet,
    fe2_screen_point first, fe2_screen_point second, uint16_t mask);
int fe2_render_color_row(fe2_render_color_scanlines *packet, int16_t row, uint16_t mask);
/* Colors are final palette indices (8 or 16). No pixel XOR is performed:
 * XOR selects a color, and ordinary solid spans write it. */
int fe2_render_color_scanlines_draw(const fe2_render_color_scanlines *packet,
    const uint8_t *colors, unsigned color_count, fe2_render_span_fn span, void *context);
int fe2_render_draw_sphere(const fe2_render_sphere_command *command, const fe2_render_backend *backend);
/* Bind to execution_target.sphere, with a fe2_render_backend context. */
void fe2_render_backend_sphere(void *backend, const fe2_render_sphere_command *command);

/* Affine textured triangle/quad drawing. 0 = textured, 2 = solid fallback
 * drawn by the original solid rasterizer, -1 = invalid/bounded-domain error.
 * Earlier pixels survive errors. Images have dimensions 1..255; the sampled
 * image must fit the original 65 x 256 workspace. Uninitialized workspace
 * samples and source reads outside the image view are rejected.
 * Low-level points are in video perimeter order. Queued quad payloads use
 * compact order; draw_texture performs their third/fourth permutation. */
uint32_t fe2_render_texture_divide(int16_t numerator, int16_t denominator);
int fe2_render_draw_texture_image(const fe2_screen_point *points, unsigned count, uint16_t selector,
    const fe2_render_texture_image *image, const uint8_t palette[256], fe2_render_span_fn span, void *context);
int fe2_render_draw_texture(const fe2_render_texture_command *command, const fe2_render_backend *backend);
/* Bind to drawing_target.texture; surface_context is the backend. */
void fe2_render_backend_texture(void *backend, const fe2_render_texture_command *command);

/* Original MCGA solid rasterizers. clipped selects the exported viewport
 * gate (at least one vertex inside) and clipped spans. Unchecked triangles/
 * quads require visible rows. Quad input is perimeter order. */
int fe2_render_solid_line(fe2_screen_point first, fe2_screen_point second, uint8_t color,
    fe2_render_span_fn span, void *context);
/* Video service 0x210 clips X spans but trusts the caller's rows, including
 * cockpit rows 168..199. A video span preserves 16-bit page wrapping. */
int fe2_render_solid_line_unchecked(fe2_screen_point first, fe2_screen_point second, uint8_t color,
    fe2_render_span_fn span, void *context);
int fe2_render_solid_polygon(const fe2_screen_point *points, unsigned count, uint8_t color,
    int clipped, fe2_render_span_fn span, void *context);
void fe2_render_backend_line(void *backend, fe2_screen_point first, fe2_screen_point second, uint16_t material);
void fe2_render_backend_surface(void *backend, const fe2_render_surface_command *command);

#endif
