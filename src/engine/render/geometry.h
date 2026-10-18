/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_RENDER_GEOMETRY_H
#define FE2_RENDER_GEOMETRY_H

#include "pipeline.h"

typedef struct fe2_render_vertex {
    int32_t camera[3];
    fe2_screen_point screen; /* previously resolved projection/cache result */
} fe2_render_vertex;

enum { FE2_RENDER_POLYGON_POINTS = 6 };
typedef struct fe2_render_polygon {
    fe2_screen_point points[FE2_RENDER_POLYGON_POINTS];
    unsigned count; /* closing point is implicit */
    uint8_t color;
} fe2_render_polygon;

/* Shared command-key calculation: unsigned wrapping sums, logical shifts,
 * high-bit priority for nonnegative depth in the normal scale range, and
 * minimum key 1 on the alternative path. Scale is the recovered word value;
 * no physical unit is assumed. */
uint32_t fe2_render_depth_key(const int32_t camera[3], int16_t scale);

/* Build the recovered four-vertex near-depth contour. A triangle is supplied
 * by repeating its third vertex in slot 3. Depth 64 is visible. Preserve cached
 * projections of visible vertices, duplicate points and winding. Returns 1
 * for a contour, 0 when all vertices are behind depth 64, -1 on invalid input.
 * Output is unchanged on rejection/error. At most six points are produced.
 * This stage does not perform material selection or viewport trivial culling. */
int fe2_render_clip_quad(const fe2_render_vertex vertices[4], uint8_t color,
                          fe2_render_polygon *polygon);

/* Collect and fill an already-built polygon through a typed span backend.
 * Returns -1 before drawing for an invalid/unrepresentable polygon. */
int fe2_render_polygon_draw(const fe2_render_polygon *polygon,
                            fe2_render_span_fn draw, void *context);

/* Queue callback: context points to fe2_render_surface, payload to polygon.
 * Callers needing draw error reporting should use polygon_draw directly. */
void fe2_render_polygon_to_surface(void *context, const void *polygon);

#endif
