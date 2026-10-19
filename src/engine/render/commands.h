/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_RENDER_COMMANDS_H
#define FE2_RENDER_COMMANDS_H

#include "geometry.h"

struct fe2_render_texture_command;

typedef struct fe2_render_segment {
    fe2_screen_point first, second;
    uint32_t key;
    uint16_t color; /* resolved RGB12/material word, before palette mapping */
} fe2_render_segment;

typedef struct fe2_render_line_target {
    fe2_render_line_fn draw;
    void *context;
} fe2_render_line_target;

typedef enum fe2_render_surface_kind {
    FE2_RENDER_SOLID_TRIANGLE, FE2_RENDER_SOLID_QUAD, FE2_RENDER_CLIPPED_POLYGON
} fe2_render_surface_kind;

typedef struct fe2_render_surface_command {
    fe2_render_polygon polygon; /* winding/duplicates retained; color supplied by backend */
    uint32_t key;
    uint16_t material;
    fe2_render_surface_kind kind;
} fe2_render_surface_command;

typedef void (*fe2_render_surface_fn)(void *context, const fe2_render_surface_command *command);
typedef struct fe2_render_drawing_target {
    fe2_render_line_target line; /* first member keeps existing line callbacks usable */
    fe2_render_surface_fn surface;
    void *surface_context;
    /* Textured triangles/quads (texture.h); receives surface_context. */
    void (*texture)(void *context, const struct fe2_render_texture_command *command);
} fe2_render_drawing_target;

/* Word at main_texture_materials-14+index. The original has no range test:
 * index 0x3f0 is image 0x29b6, the encoding of `mov bx, ax`. */
int fe2_render_material_word(uint32_t index, uint16_t *out);

/* Resolve the line producer's packed material selector against the immutable
 * native material resource. Word additions wrap independently before the
 * inherited dword addition. A missing table word returns -1 unchanged. */
int fe2_render_line_material(uint16_t token, uint16_t face_base, uint32_t inherited, uint16_t *out);

/* Native line-command producer, after vertex resolution.
 * low/high are the signed-byte references in the stream word, respectively.
 * Preserves cached projections, endpoint ordering, sentinel culling and the
 * original sort-key anchor changes. Returns 1 for a built segment, 0 when
 * culled, -1 for an invalid material/input; output unchanged on 0/-1.
 * Palette mapping and node allocation belong to the caller. */
int fe2_render_build_segment(const fe2_render_vertex *low, const fe2_render_vertex *high,
                               uint16_t token, uint16_t face_base, uint32_t inherited,
                               int16_t scale, fe2_render_segment *out);

/* Queue callback. Target supplies the real line primitive; payload is a
 * built segment whose color the caller may have mapped to a palette slot. */
void fe2_render_segment_execute(void *target, const void *segment);

/* Solid-mode surface producer, after face classification/vertex resolution.
 * records are in the clipped builder's order. cached contains the projections
 * captured during resolution in compact triangle/quad order (these can differ
 * from final cache values after paired generation). Count is 3 or 4.
 * A negative face word culls before material lookup. No texture fallback is
 * silently substituted: this API explicitly implements the solid mode.
 * Returns 1 emitted, 0 culled, -1 invalid; output unchanged on 0/-1. */
int fe2_render_build_surface(const fe2_render_vertex *records, const fe2_screen_point *cached,
                               unsigned count, uint16_t token, uint16_t face,
                               uint32_t inherited, int16_t scale, fe2_render_surface_command *out);
void fe2_render_surface_execute(void *target, const void *command);

extern const uint8_t *fe2_render_material_data;
extern uint32_t fe2_render_material_bytes;
extern const uint16_t *fe2_render_pair_rejection;

#endif
